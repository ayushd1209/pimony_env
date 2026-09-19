
#include <stdint.h>

#ifdef FREESTANDING
#define K_INF __builtin_inff()
#else
#include <math.h>
#include <stdio.h>
#define K_INF INFINITY
#endif

/* ---------------- model configuration ---------------- */

#define HIDDEN   768              /* BERT-base d_model                        */
#define HEADS    12
#define HEAD_DIM (HIDDEN / HEADS) /* 64                                       */
#define FFN      3072             /* intermediate_size                        */
#ifndef SEQ
#define SEQ      1                /* tokens per pass. 1 = GEMV (no weight reuse,
                                   * decode-shaped). >1 = GEMM, which is how BERT
                                   * is actually run. Set with -DSEQ=n */
#endif
#define ITERS    1                /* repeat the layer, for timing runs        */
#define LN_EPS   1e-5f            /* torch.nn.LayerNorm default               */

/* ---------------- gem5 pseudo-instructions ---------------- */
/* Encodings copied from tests/test-progs/fence_e2e/main.c. For the stats
 * markers a0=delay a1=period, both 0 = dump once, now. No-ops off RISC-V so
 * the same source builds natively for validation. */

#ifdef __riscv
static inline void m5_reset_stats(void) {
    register uint64_t a0 asm("a0") = 0, a1 asm("a1") = 0;
    __asm__ volatile(".word 0x8000007B" ::"r"(a0), "r"(a1) : "memory");
}
static inline void m5_dump_reset_stats(void) {
    register uint64_t a0 asm("a0") = 0, a1 asm("a1") = 0;
    __asm__ volatile(".word 0x8400007B" ::"r"(a0), "r"(a1) : "memory");
}
/* Dump WITHOUT resetting (M5OP_DUMP_STATS 0x41). A gem5 stats reset also calls
 * DRAMsim3::resetStats(), which wipes PIMony's PIM command counters -- and
 * PIMony only writes dramsim3.txt on the exit callback, so resetting at the end
 * of the ROI erases exactly the counts we want. The first stats block is
 * identical either way: the reset at ROI START is what scopes it. */
static inline void m5_dump_stats(void) {
    register uint64_t a0 asm("a0") = 0, a1 asm("a1") = 0;
    __asm__ volatile(".word 0x8200007B" ::"r"(a0), "r"(a1) : "memory");
}
static inline void m5_exit(void) {
    register uint64_t a0 asm("a0") = 0;
    __asm__ volatile(".word 0x4200007B" ::"r"(a0) : "memory");
}
#else
static inline void m5_reset_stats(void) {}
static inline void m5_dump_reset_stats(void) {}
static inline void m5_dump_stats(void) {}
static inline void m5_exit(void) {}
#endif

/* Per-section timing, -DPHASE_STATS. Each call dumps and resets, so stats block
 * N is section N of bert_layer(). Off by default: the plain build is unchanged. */
#ifdef PHASE_STATS
#define PHASE() m5_dump_reset_stats()
#else
#define PHASE() ((void)0)
#endif

/* ---------------- output without a C library ----------------
 * gem5's SE mode emulates Linux syscalls, so we can write to stdout with a
 * raw ecall -- no libc needed. Build with -DNO_IO for bare-metal FS mode,
 * where an ecall would trap instead. */

#if defined(FREESTANDING) && !defined(NO_IO)
#define HAVE_IO 1

static void sys_write(const char *p, unsigned long n) {
    register long a0 asm("a0") = 1;              /* fd 1 = stdout */
    register const char *a1 asm("a1") = p;
    register unsigned long a2 asm("a2") = n;
    register long a7 asm("a7") = 64;             /* SYS_write */
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
}

static void put_str(const char *s) {
    unsigned long n = 0;
    while (s[n]) n++;
    sys_write(s, n);
}

/* label, then v to 6 decimal places, then newline */
static void put_num(const char *label, double v) {
    char buf[80];
    int n = 0;
    while (*label) buf[n++] = *label++;
    if (v < 0.0) { buf[n++] = '-'; v = -v; }

    long long scaled = (long long)(v * 1000000.0 + 0.5);
    long long ip = scaled / 1000000, fp = scaled % 1000000;

    char tmp[24];
    int t = 0;
    if (ip == 0) tmp[t++] = '0';
    while (ip > 0) { tmp[t++] = (char)('0' + (int)(ip % 10)); ip /= 10; }
    while (t > 0) buf[n++] = tmp[--t];

    buf[n++] = '.';
    for (long long d = 100000; d > 0; d /= 10)
        buf[n++] = (char)('0' + (int)((fp / d) % 10));
    buf[n++] = '\n';
    sys_write(buf, n);
}
#endif

/* ---------------- math ----------------
 * sqrt is a hardware instruction (fsqrt.s) via the builtin, so it never needs
 * libm. exp and tanh are hand-rolled under FREESTANDING: accurate to about
 * 1 fp32 ulp, and keeping them in-tree means they show up as ordinary
 * instructions in the gem5 profile instead of an opaque libm blob. */

#define k_sqrtf(v) __builtin_sqrtf(v)

#ifdef FREESTANDING

/* e^v via 2^n * e^r, r = v - n*ln2, |r| <= ln2/2; degree-6 Taylor on r */
static float k_expf(float v) {
    if (v >  87.0f) return K_INF;
    if (v < -87.0f) return 0.0f;
    const float LOG2E = 1.4426950408889634f;
    const float LN2   = 0.6931471805599453f;
    float fn = v * LOG2E;
    int n = (int)(fn + (fn >= 0.0f ? 0.5f : -0.5f));
    float r = v - (float)n * LN2;
    float p = 1.0f + r * (1.0f + r * (0.5f + r * (0.16666667f
              + r * (0.041666668f + r * (0.008333334f + r * 0.0013888889f)))));
    union { float f; uint32_t u; } s;
    s.u = (uint32_t)((127 + n) << 23);          /* 2^n */
    return p * s.f;
}

/* tanh via exp; series near zero to dodge the (e-1)/(e+1) cancellation */
static float k_tanhf(float v) {
    if (v >  9.0f) return  1.0f;
    if (v < -9.0f) return -1.0f;
    if (v > -0.05f && v < 0.05f) {
        float v2 = v * v;
        return v * (1.0f - v2 * (0.33333334f - v2 * 0.13333334f));
    }
    float e = k_expf(2.0f * v);
    return (e - 1.0f) / (e + 1.0f);
}

#else
#define k_expf(v)  expf(v)
#define k_tanhf(v) tanhf(v)
#endif

/* ---------------- PRNG contract ----------------
 * To reproduce these weights in Python:
 *
 *   state = 20260816
 *   def u32():
 *       global state
 *       state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
 *       return state
 *   def uni():                      # exact in binary32: 2**24 / 2**23
 *       return np.float32((u32() >> 8) / 8388608.0 * 2.0 - 1.0)
 *
 * Then fill, in THIS order, each element as uni()*scale with
 * scale = 1/sqrt(fan_in) -- matching torch.nn.Linear's default init:
 *   Wq bq  Wk bk  Wv bv  Wo bo  W1 b1  W2 b2  g1 beta1  g2 beta2  x
 * LayerNorm gain is 1.0 + uni()*0.1, its bias is uni()*0.1.
 * Weights are row-major [out][in] -- the same layout as PyTorch's
 * Linear.weight, so copying into a state_dict needs no transpose.
 */
#define SEED 20260816u

static uint32_t rng_state;

static uint32_t rng_u32(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

/* uniform in [-1,1); the shift and divide are exact in binary32 */
static float rng_uniform(void) {
    return (float)(rng_u32() >> 8) / 8388608.0f * 2.0f - 1.0f;
}

static void fill(float *p, uint64_t n, float scale) {
    for (uint64_t i = 0; i < n; i++) p[i] = rng_uniform() * scale;
}

/* ---------------- weight storage type ----------------
 * The PIM engine consumes FP16: one 32 B column step = 16 FP16 values, so a
 * 768-long dot is 48 steps and fits inside one 2 KB row of one bank. fp32's 96
 * steps would not. -DPIM_FP16 on its own gives an FP16 CPU baseline, so the
 * host comparison can be made against the same precision. */
#if defined(PIM_FP16) || defined(PIM_LAYOUT)
typedef _Float16 wt_t;
#define WT_PER_STEP 16                 /* 32 B step / 2 B */
#else
typedef float wt_t;
#define WT_PER_STEP 8                  /* 32 B step / 4 B */
#endif

static void fill_w(wt_t *p, uint64_t n, float scale) {
    for (uint64_t i = 0; i < n; i++) p[i] = (wt_t)(rng_uniform() * scale);
}

/* ---------------- PIM weight placement ----------------
 * A MAC command names (bankgroup, row, step-range) -- it carries NO bank field
 * (pim_controller.cc TransToCommand sets addr.bank = -1), so it broadcasts to
 * all 4 banks of that bankgroup. Each bank has its own multiplier and its own
 * result register, and nothing can add one bank's register to another's.
 * Two consequences, and they are the whole layout:
 *   - one output's weights must sit entirely in ONE bank
 *   - the 4 banks under a command hold 4 DIFFERENT outputs, computed together
 *
 *   128 MAC units = 32 streams (channel, rank, bankgroup) x 4 banks
 *
 *   output j ->  unit  j % 128     ->  bank = unit % 4 , stream = unit / 4
 *                wave  j / 128     ->  which DRAM row
 *   input  i ->  step  i / 16      ->  position along the dot
 *                slot  i % 16      ->  position inside the 32 B step
 *
 * A row holds 64 steps (2 KB / 32 B). In FP16 a 768-long dot is 48 -> fits, one
 * command. A 3072-long dot is 192 -> 3 rows, issued as 3 commands whose partial
 * sums the host adds; the placement below handles that with rows_per_wave, so
 * no array has to be split.
 *
 * Strides from LPDDR5X_12Gb_x16_8533_pimony.ini (rorabacobgch,
 * request_size_bytes = 32), measured against Config::AddressMapping.
 * Enable with -DPIM_LAYOUT. NOTE: it invalidates the CPU compute path in
 * bert_layer() -- these weights are laid out for PIM to consume. */
#ifdef PIM_LAYOUT

#define PIM_UNITS      128                 /* 32 streams x 4 banks           */
#define PIM_STEPS_ROW  64                  /* 2 KB row / 32 B step           */

/* strides in wt_t elements (byte stride / 2) */
#define PIM_S_CH       16                  /*  32 B  */
#define PIM_S_BG       64                  /* 128 B  */
#define PIM_S_STEP     256                 /* 512 B  */
#define PIM_S_BANK     16384               /*  32 KB */
#define PIM_S_RANK     65536               /* 128 KB */
#define PIM_S_ROW      131072              /* 256 KB = 128 banks x 2 KB      */

/* Longest reduction one command can do: one row of FP16 = 1024 */
#define PIM_MAX_IN     (PIM_STEPS_ROW * WT_PER_STEP)
#define PIM_PARTS(in)  (((in) + PIM_MAX_IN - 1) / PIM_MAX_IN)
#define PIM_WAVES(out) (((out) + PIM_UNITS - 1) / PIM_UNITS)

/* index of weight i of output j WITHIN one partial matmul (in_dim <= 1024),
 * in wt_t elements. step <= 63, so it never leaves the row. */
static uint64_t pim_idx(int j, int i) {
    int unit   = j % PIM_UNITS;
    int bank   = unit % 4;
    int stream = unit / 4;                 /* 0..31 */
    int ch     = stream % 4;
    int bg     = (stream / 4) % 4;
    int rank   = stream / 16;
    int step   = i / WT_PER_STEP;          /* position along the dot */
    int row    = j / PIM_UNITS;            /* which wave of 128 outputs */

    return (uint64_t)ch   * PIM_S_CH
         + (uint64_t)bg   * PIM_S_BG
         + (uint64_t)rank * PIM_S_RANK
         + (uint64_t)bank * PIM_S_BANK
         + (uint64_t)row  * PIM_S_ROW
         + (uint64_t)step * PIM_S_STEP
         + (uint64_t)(i % WT_PER_STEP);
}

/* Input vector, for the GWRITE phase. A D2GWRITE loads a DRAM row into the
 * channel's global buffer, and a channel's buffer can only be fed from that
 * channel's own DRAM -- so the vector must exist FOUR TIMES, once per channel.
 * Element i sits at the same step/slot the weights use, so the buffer feeds the
 * MAC units in the order they consume it.
 * bg/rank/bank are all 0: the source only has to be SOME bank in the channel.
 * The four copies interleave inside the same 512 B column stride (channel is
 * only 32 B apart), so this costs one row, not four. */
static uint64_t pim_vec_idx(int ch, int i) {
    return (uint64_t)ch             * PIM_S_CH
         + (uint64_t)(i / WT_PER_STEP) * PIM_S_STEP
         + (uint64_t)(i % WT_PER_STEP);
}

/* Parts sit PIM_S_ROW apart, matching fill_pim: only the row field changes. */
#define PIM_VEC_ELEMS ((PIM_PARTS(FFN) - 1) * PIM_S_ROW \
                       + PIM_STEPS_ROW * PIM_S_STEP)

/* A reduction longer than PIM_MAX_IN is stored as several INDEPENDENT partial
 * matmuls, each in its own row-aligned block, each a plain pim.gemv; the host
 * sums the partial results. Storing them interleaved instead would force the
 * sequencer to be told the placement stride and the partial index -- two extra
 * operands -- so the split lives here, not in the ISA.
 * Same RNG draw order as fill(), so the weight values are unchanged. */
static void fill_pim(wt_t *p, int out_dim, int in_dim, float scale) {
    uint64_t block = (uint64_t)PIM_WAVES(out_dim) * PIM_S_ROW;
    for (int j = 0; j < out_dim; j++)
        for (int i = 0; i < in_dim; i++) {
            int part = i / PIM_MAX_IN;
            p[part * block + pim_idx(j, i - part * PIM_MAX_IN)] =
                (wt_t)(rng_uniform() * scale);
        }
}

#define FILL_W(w, out, in, s) fill_pim(&(w)[0][0], (out), (in), (s))

/* Padded first dimension. One wave of 128 outputs occupies a whole machine-wide
 * row (PIM_S_ROW) even when the dot uses only 48 of the row's 64 steps, so a
 * 768x768 matrix costs 1.33x. W2's 3072-long dot fills 3 rows exactly -- no
 * waste. Evaluate left to right: waves * rows * PIM_S_ROW divides by in_dim. */
#define WDIM0(out, in) (PIM_PARTS(in) * PIM_WAVES(out) * PIM_S_ROW / (in))

/* The array MUST start on a 256 KB boundary. Every field above rank -- and rank
 * itself -- is addressed by offset from the base, so a base that is not a whole
 * number of rows carries into rank or bank partway through a matrix and splits
 * dots across engines. Measured: 512 B / 32 KB / 64 KB bases all break. */
#define PIM_ALIGN __attribute__((aligned(1 << 18)))
#else
#define WDIM0(out, in) (out)
#define FILL_W(w, out, in, s) fill_w(&(w)[0][0], (uint64_t)(out) * (in), (s))
#define PIM_ALIGN
#endif

/* ---------------- parameters (~28 MB, lives in .bss) ---------------- */

/* WDIM0 pads the output dimension out to whole DRAM rows under -DPIM_LAYOUT and
 * is a no-op otherwise. Every use goes through &W[0][0], so padding is
 * invisible to the call sites. */
static wt_t Wq[WDIM0(HIDDEN, HIDDEN)][HIDDEN] PIM_ALIGN; static float bq[HIDDEN];
static wt_t Wk[WDIM0(HIDDEN, HIDDEN)][HIDDEN] PIM_ALIGN; static float bk[HIDDEN];
static wt_t Wv[WDIM0(HIDDEN, HIDDEN)][HIDDEN] PIM_ALIGN; static float bv[HIDDEN];
static wt_t Wo[WDIM0(HIDDEN, HIDDEN)][HIDDEN] PIM_ALIGN; static float bo[HIDDEN];
static wt_t W1[WDIM0(FFN, HIDDEN)][HIDDEN]    PIM_ALIGN; static float b1[FFN];
static wt_t W2[WDIM0(HIDDEN, FFN)][FFN]       PIM_ALIGN; static float b2[HIDDEN];
static float g1[HIDDEN], beta1[HIDDEN];        /* norm after attention */
static float g2[HIDDEN], beta2[HIDDEN];        /* norm after FFN       */

#ifdef PIM_LAYOUT
/* Staging buffer for the GWRITE phase, refilled before every pim.gemv. Unlike
 * the weights this is host work on the critical path -- 4n scattered stores per
 * matmul -- so it belongs inside whatever region gets timed. */
static wt_t pimvec[PIM_VEC_ELEMS] PIM_ALIGN;

static void pim_store_vec(const float *v, int n) {
    for (int i = 0; i < n; i++) {
        int part  = i / PIM_MAX_IN;
        int local = i - part * PIM_MAX_IN;
        wt_t e = (wt_t)v[i];
        for (int ch = 0; ch < 4; ch++)
            pimvec[(uint64_t)part * PIM_S_ROW + pim_vec_idx(ch, local)] = e;
    }
}
#define PIM_STORE_VEC(v, n) pim_store_vec((v), (n))
#else
#define PIM_STORE_VEC(v, n) ((void)0)
#endif

/* ---------------- pim.gemv offload (-DPIM_GEMV) ---------------- */
#ifdef PIM_GEMV
#ifndef PIM_LAYOUT
#error "PIM_GEMV needs PIM_LAYOUT: the offload consumes the PIM weight layout"
#endif

/* A memory request carries ONE address but the offload needs THREE, so they
 * live in memory and rs1 points at the struct. _Alignas(32) = its own size, so
 * the device's fetch cannot straddle two chunks (the compiler pads to 32). */
struct pim_gemv_desc { uint64_t w_base; uint64_t v_base; uint64_t out_base; }
    __attribute__((aligned(32)));

/* One per pim.gemv instruction, all in ONE array so they share a DRAM row:
 * only the first descriptor pays the row activate.
 *   Wq Wk Wv Wo W1 = 1 each, W2 = PIM_PARTS(FFN) = 3  ->  8 */
#define PIM_NDESC (5 + PIM_PARTS(FFN))
static struct pim_gemv_desc g_desc[PIM_NDESC];   /* 32 B stride from the type */

/* --- instruction wrappers, verbatim from tests/test-progs/pim_gemv/main.c --- */

/* -DNO_PIM_FENCE is the CONTROL BUILD for the fence A/B: the instruction is
 * dropped, the "memory" clobber stays. The clobber must stay or -O3 folds
 * pimout to its zero initialiser and deletes the whole read-back -- the control
 * would then measure the fences AND the read-back at once. */
#ifdef NO_PIM_FENCE
#define PIM_CMO(word) ""
#else
#define PIM_CMO(word) word
#endif

/* pim.fence.cl a0 -> custom-0 funct3=1: push the descriptor's line out to DRAM
 * so the device can see it. Required by the offload contract. */
static inline void pim_fence_cl(const void *p) {
    register uint64_t a0 asm("a0") = (uint64_t)p;
    __asm__ volatile(PIM_CMO(".word 0x0005100B") :: "r"(a0) : "memory");
}

/* pim.fence.inv a0 -> custom-0 funct3=2: drop that line so the next load sees
 * what the device wrote to DRAM. The mirror of fence.cl, on the result path. */
static inline void pim_fence_inv(const void *p) {
    register uint64_t a0 asm("a0") = (uint64_t)p;
    __asm__ volatile(PIM_CMO(".word 0x0005200B") :: "r"(a0) : "memory");
}

/* Invalidate a result range, one 64 B line at a time -- there is no range
 * fence. The "memory" clobber is load-bearing twice: it is the fence, and it
 * also stops -O3 folding pimout to its zero initialiser, since the CPU never
 * writes that array and the compiler cannot see the device's writes. */
static void pim_inv_range(const void *p, unsigned long bytes) {
    const char *q = (const char *)p;
    for (unsigned long i = 0; i < bytes; i += 64) pim_fence_inv(q + i);
}

/* The staged vector is written with ordinary stores, so it sits dirty in cache
 * and D2GWRITE would read stale DRAM. Clean it. Each 512 B stride holds 128
 * useful bytes (4 channel copies x 32 B) = 2 lines; the other 384 B are padding
 * nobody wrote. n elements -> n/16 strides -> n/8 fences. */
static void pim_flush_vec(int n) {
    for (int i = 0; i < n; i += WT_PER_STEP) {
        int part  = i / PIM_MAX_IN;
        int local = i - part * PIM_MAX_IN;
        const char *p = (const char *)&pimvec[(uint64_t)part * PIM_S_ROW
                        + (uint64_t)(local / WT_PER_STEP) * PIM_S_STEP];
        pim_fence_cl(p);
        pim_fence_cl(p + 64);
    }
}
#define PIM_FLUSH_VEC(n) pim_flush_vec(n)

/* pim.gemv a2, a0, a1 -> custom-0 funct3=5.
 *   a0 = descriptor pointer      (rs1)
 *   a1 = dot_steps | outputs<<16 (rs2)
 *   a2 = completion token        (rd)   -- returns immediately, PIM runs on */
static inline uint64_t pim_gemv(const struct pim_gemv_desc *d, uint32_t outputs,
                                uint32_t dot_steps) {
    register uint64_t a0 asm("a0") = (uint64_t)d;
    register uint64_t a1 asm("a1") = (uint64_t)dot_steps
                                   | ((uint64_t)outputs << 16);
    register uint64_t a2 asm("a2");
    __asm__ volatile(".word 0x00B5560B" : "=r"(a2) : "r"(a0), "r"(a1) : "memory");
    return a2;
}

/* pim.wait a0 -> custom-0 funct3=0: block until that token completes. */
static inline uint64_t pim_wait(uint64_t token) {
    register uint64_t a0 asm("a0") = token;
    register uint64_t a2 asm("a2");
    __asm__ volatile(".word 0x0005060B" : "=r"(a2) : "r"(a0));
    return a2;
}

/* Where the device puts results. It writes FP16 at out_base + j*2, so this is
 * wt_t, not float -- the host converts on read-back, which is what a real host
 * pays. One slab per partial matmul, stride out_dim. Biggest user is W1 (1 part
 * x 3072); W2 is 3 x 768. aligned(8) = banks_per_group x result width, the base
 * alignment the sequencer enforces; every slab base inherits it because out_dim
 * is a multiple of 4 here (1536 B and 6144 B strides).
 * RESULT WIDTH IS AN ASSUMPTION: the paper gives operand widths (16 x FP16 from
 * the BLSA, 16 from the global buffer) but never a result or accumulator width,
 * so FP16 is our choice, not a reading. Sensitivity is one line (kResultBytes
 * 2 -> 4) and would take result traffic from 0.77% of a layer to ~1.5%. */
#define PIM_OUT_SLOTS (FFN > PIM_PARTS(FFN) * HIDDEN ? FFN : PIM_PARTS(FFN) * HIDDEN)
static wt_t pimout[PIM_OUT_SLOTS] __attribute__((aligned(8)));

/* Fill descriptor di and fire ONE pim.gemv; return its token.
 *   W        = PIM-laid-out weight array
 *   out_dim  = outputs of the whole matmul
 *   in_dim   = full reduction length (3072 for W2, 768 for the rest)
 *   part     = which 1024-long slab; 0 for everything except W2
 * Deliberately does NOT wait -- the caller places the wait, so overlapping
 * independent matmuls later needs no change here. */
static inline uint64_t pim_issue(int di, const wt_t *W, void *out, int out_dim,
                                 int in_dim, int part) {
    int rem = in_dim - part * PIM_MAX_IN;                  /* left after this */
    int len = rem < PIM_MAX_IN ? rem : PIM_MAX_IN;         /* this slab's dot */
    uint64_t block = (uint64_t)PIM_WAVES(out_dim) * PIM_S_ROW;  /* slab stride */

    g_desc[di].w_base = (uint64_t)(W + (uint64_t)part * block);
    g_desc[di].v_base = (uint64_t)(pimvec + (uint64_t)part * PIM_S_ROW);
    g_desc[di].out_base = (uint64_t)out;                   /* this slab's pimout */
    pim_fence_cl(&g_desc[di]);                             /* descriptor -> DRAM */
    return pim_gemv(&g_desc[di], out_dim, len / WT_PER_STEP);
}

/* One whole matmul on PIM, drop-in for linear(). Strictly issue->wait: the
 * sequencer holds ONE job at a time (memory_system.h GemvJob), because a single
 * pim.gemv already spans all 32 engines.
 * A reduction longer than PIM_MAX_IN arrives as `parts` independent partial
 * arrays; summing them is the host's job and it is done here, so W2 now pays
 * the ~2*out_dim adds and their traffic instead of understating them. */
static void pim_linear(int di, const wt_t *W, const float *b, float *y,
                       int out_dim, int in_dim) {
    int parts = PIM_PARTS(in_dim);
    for (int p = 0; p < parts; p++) {
        uint64_t tok = pim_issue(di + p, W, pimout + (unsigned long)p * out_dim,
                                 out_dim, in_dim, p);
        pim_wait(tok);                     /* token stays in a register */
    }
    /* The device wrote DRAM behind the cache; drop those lines before reading. */
    pim_inv_range(pimout, (unsigned long)parts * out_dim * sizeof(wt_t));

    for (int o = 0; o < out_dim; o++) {
        float acc = b[o];                  /* bias */
        for (int p = 0; p < parts; p++)    /* + each slab's partial sum */
            acc += (float)pimout[(unsigned long)p * out_dim + o];
        y[o] = acc;
    }
}

/* Descriptor slots. W2 owns 5..7, one per slab. */
#define D_WQ 0
#define D_WK 1
#define D_WV 2
#define D_WO 3
#define D_W1 4
#define D_W2 5

/* --- completion path, verbatim from tests/test-progs/pim_gemv/main.c --- */
/* The PIM raises local interrupt 24 when a job finishes; this handler reads the
 * done-mask over MMIO and hands each token back to the CPU scoreboard. Without
 * it pim_wait never wakes. aligned(4) is load-bearing: mtvec/stvec's low 2 bits
 * are the MODE field, so a 2-byte-aligned handler corrupts it. */
#define PIM_DONE       (*(volatile uint64_t *)0x100000000ULL)
#define PIM_TOKEN_ASID ((volatile uint64_t *)0x100000008ULL)

volatile uint64_t handler_calls = 0;       /* expect PIM_NDESC at the end */

__attribute__((interrupt("supervisor"), aligned(4)))
void trap_handler(void) {
    handler_calls++;
    uint64_t mask = PIM_DONE;
    for (int t = 0; t < 64; t++) {
        if (mask & (1ULL << t)) {
            uint64_t asid = PIM_TOKEN_ASID[t];
            uint64_t pack = (asid << 16) | (uint64_t)t;
            __asm__ volatile("csrw 0x801, %0" :: "r"(pack));
        }
    }
    PIM_DONE = mask;                       /* W1C ack */
}

/* Sv39 identity paging: one gigapage for DRAM, one for the PIM MMIO window. */
static uint64_t root_table[512] __attribute__((aligned(4096)));
static uint64_t make_gigapage(uint64_t pa) { return ((pa >> 12) << 10) | 0xCF; }
static void setup_paging(void) {
    root_table[2] = make_gigapage(0x80000000ULL);
    root_table[4] = make_gigapage(0x100000000ULL);
    uint64_t satp = (8ULL << 60) | (1ULL << 44) | ((uint64_t)root_table >> 12);
    __asm__ volatile("csrw satp, %0" :: "r"(satp));
    __asm__ volatile("sfence.vma");
}

#define LINEAR(di, W, b, in, y, o, i) \
    pim_linear((di), &(W)[0][0], (b), (y), (o), (i))
#else
#define PIM_FLUSH_VEC(n) ((void)0)
#define LINEAR(di, W, b, in, y, o, i) \
    linear(&(W)[0][0], (b), (in), (y), (o), (i))
#define D_WQ 0
#define D_WK 0
#define D_WV 0
#define D_WO 0
#define D_W1 0
#define D_W2 0
#endif  /* PIM_GEMV */

/* ---------------- activations ---------------- */

/* No longer PIM write targets -- results land in pimout and the host converts
 * into these. Kept aligned so a load of a converted row is not split. */
#define PIM_OUT_ALIGN __attribute__((aligned(8)))

static float x[SEQ][HIDDEN];                   /* layer input          */
static float Q[SEQ][HIDDEN] PIM_OUT_ALIGN, K[SEQ][HIDDEN] PIM_OUT_ALIGN,
             V[SEQ][HIDDEN] PIM_OUT_ALIGN;
static float att[HEADS][SEQ][SEQ];             /* scores, then weights */
static float ctx[SEQ][HIDDEN];                 /* heads concatenated   */
static float proj[SEQ][HIDDEN] PIM_OUT_ALIGN;                /* attention output     */
static float norm1[SEQ][HIDDEN];
static float hid[SEQ][FFN] PIM_OUT_ALIGN;                    /* FFN intermediate     */
static float ff[SEQ][HIDDEN] PIM_OUT_ALIGN;
static float out[SEQ][HIDDEN];
static int   mask[SEQ];                        /* 1 = attend, 0 = ignore */

/* results, volatile so they survive -O3 and can be read from a memory dump */
volatile float  g_out[4];
volatile double g_checksum;

/* ---------------- kernels ---------------- */

/* y[t][o] = b[o] + sum_i W[o][i] * in[t][i]
 * THE hot loop: >99% of this layer's multiply-accumulates land here, and the
 * inner loop is contiguous in both operands. This is the PIM offload target. */
static void linear(const wt_t *W, const float *b, const float *in, float *y,
                   int n_out, int n_in) {
    /* Output loop OUTERMOST, token loop inside: each weight row is fetched once
     * and consumed by all SEQ tokens while it is still in L1. With the loops the
     * other way round the whole 28 MB of weights is re-walked per token, so
     * weight traffic scales with SEQ and no reuse is captured at all.
     * At SEQ=1 the two orders are identical. */
    for (int o = 0; o < n_out; o++) {
        const wt_t *w = W + (uint64_t)o * n_in;
        for (int t = 0; t < SEQ; t++) {
            const float *v = in + (uint64_t)t * n_in;
            float s = b[o];
            for (int i = 0; i < n_in; i++) s += (float)w[i] * v[i];
            y[(uint64_t)t * n_out + o] = s;
        }
    }
}

/* per token: (in - mean)/sqrt(var + eps) * gain + bias. Biased variance, as
 * torch does it. */
static void layernorm(const float *in, const float *gain, const float *bias,
                      float *y) {
    for (int t = 0; t < SEQ; t++) {
        const float *v = in + (uint64_t)t * HIDDEN;
        float mean = 0.0f;
        for (int i = 0; i < HIDDEN; i++) mean += v[i];
        mean /= (float)HIDDEN;
        float var = 0.0f;
        for (int i = 0; i < HIDDEN; i++) { float d = v[i] - mean; var += d * d; }
        var /= (float)HIDDEN;
        float inv = 1.0f / k_sqrtf(var + LN_EPS);
        for (int i = 0; i < HIDDEN; i++)
            y[(uint64_t)t * HIDDEN + i] = (v[i] - mean) * inv * gain[i] + bias[i];
    }
}

/* tanh form -- match with F.gelu(x, approximate='tanh') on the Python side.
 * The default erf form would need erff(). */
static float gelu(float v) {
    const float c = 0.7978845608028654f;   /* sqrt(2/pi) */
    return 0.5f * v * (1.0f + k_tanhf(c * (v + 0.044715f * v * v * v)));
}

static void residual_add(const float *a, const float *b_, float *y, int n) {
    for (int i = 0; i < n; i++) y[i] = a[i] + b_[i];
}

/* Multi-head self-attention over Q/K/V, heads concatenated into ctx.
 * Head h owns columns [h*HEAD_DIM, (h+1)*HEAD_DIM) -- the PyTorch version's
 * reshape+transpose is just this offset, no data movement. */
static void attention(void) {
    const float scale = 1.0f / k_sqrtf((float)HEAD_DIM);

    for (int h = 0; h < HEADS; h++) {
        const int off = h * HEAD_DIM;
        for (int i = 0; i < SEQ; i++) {
            /* scores: dot(query i, key j) for every j */
            for (int j = 0; j < SEQ; j++) {
                float s = 0.0f;
                for (int d = 0; d < HEAD_DIM; d++)
                    s += Q[i][off + d] * K[j][off + d];
                att[h][i][j] = mask[j] ? s * scale : -K_INF;
            }

            /* softmax over j, max-subtracted. Assumes >=1 unmasked position. */
            float m = att[h][i][0];
            for (int j = 1; j < SEQ; j++) if (att[h][i][j] > m) m = att[h][i][j];
            float sum = 0.0f;
            for (int j = 0; j < SEQ; j++) {
                att[h][i][j] = k_expf(att[h][i][j] - m);
                sum += att[h][i][j];
            }
            for (int j = 0; j < SEQ; j++) att[h][i][j] /= sum;

            /* context: weighted sum of value vectors */
            for (int d = 0; d < HEAD_DIM; d++) {
                float s = 0.0f;
                for (int j = 0; j < SEQ; j++) s += att[h][i][j] * V[j][off + d];
                ctx[i][off + d] = s;
            }
        }
    }
}

/* ---------------- the layer ---------------- */

static void bert_layer(void) {
    /* self-attention block. One store serves Q/K/V -- they share an input. */
    PIM_STORE_VEC(&x[0][0], HIDDEN);
    PIM_FLUSH_VEC(HIDDEN);                 /* one flush serves Q/K/V */
    LINEAR(D_WQ, Wq, bq, &x[0][0], &Q[0][0], HIDDEN, HIDDEN);
    LINEAR(D_WK, Wk, bk, &x[0][0], &K[0][0], HIDDEN, HIDDEN);
    LINEAR(D_WV, Wv, bv, &x[0][0], &V[0][0], HIDDEN, HIDDEN);
    PHASE();                               /* [1] QKV                        */

    attention();                           /* stays on the CPU by design */
    PHASE();                               /* [2] attention                  */

    PIM_STORE_VEC(&ctx[0][0], HIDDEN);
    PIM_FLUSH_VEC(HIDDEN);
    LINEAR(D_WO, Wo, bo, &ctx[0][0], &proj[0][0], HIDDEN, HIDDEN);
    PHASE();                               /* [3] Wo                         */

    /* residual + norm */
    residual_add(&x[0][0], &proj[0][0], &proj[0][0], SEQ * HIDDEN);
    layernorm(&proj[0][0], g1, beta1, &norm1[0][0]);
    PHASE();                               /* [4] residual + layernorm 1     */

    /* feed-forward block */
    PIM_STORE_VEC(&norm1[0][0], HIDDEN);
    PIM_FLUSH_VEC(HIDDEN);
    LINEAR(D_W1, W1, b1, &norm1[0][0], &hid[0][0], FFN, HIDDEN);
    PHASE();                               /* [5] W1                         */

    for (int i = 0; i < SEQ * FFN; i++) (&hid[0][0])[i] = gelu((&hid[0][0])[i]);
    PHASE();                               /* [6] GELU                       */

    PIM_STORE_VEC(&hid[0][0], FFN);
    PIM_FLUSH_VEC(FFN);
    LINEAR(D_W2, W2, b2, &hid[0][0], &ff[0][0], HIDDEN, FFN);
    PHASE();                               /* [7] W2                         */

    /* residual + norm */
    residual_add(&norm1[0][0], &ff[0][0], &ff[0][0], SEQ * HIDDEN);
    layernorm(&ff[0][0], g2, beta2, &out[0][0]);
    PHASE();                               /* [8] residual + layernorm 2     */
}

static void init_params(void) {
    const float sh = 1.0f / k_sqrtf((float)HIDDEN);   /* fan_in = 768  */
    const float sf = 1.0f / k_sqrtf((float)FFN);      /* fan_in = 3072 */

    rng_state = SEED;

    FILL_W(Wq, HIDDEN, HIDDEN, sh); fill(bq, HIDDEN, sh);
    FILL_W(Wk, HIDDEN, HIDDEN, sh); fill(bk, HIDDEN, sh);
    FILL_W(Wv, HIDDEN, HIDDEN, sh); fill(bv, HIDDEN, sh);
    FILL_W(Wo, HIDDEN, HIDDEN, sh); fill(bo, HIDDEN, sh);

    FILL_W(W1, FFN, HIDDEN, sh);    fill(b1, FFN, sh);
    FILL_W(W2, HIDDEN, FFN, sf);    fill(b2, HIDDEN, sf);

    fill(g1, HIDDEN, 0.1f); for (int i = 0; i < HIDDEN; i++) g1[i] += 1.0f;
    fill(beta1, HIDDEN, 0.1f);
    fill(g2, HIDDEN, 0.1f); for (int i = 0; i < HIDDEN; i++) g2[i] += 1.0f;
    fill(beta2, HIDDEN, 0.1f);

    fill(&x[0][0], (uint64_t)SEQ * HIDDEN, 1.0f);     /* layer input */
    for (int t = 0; t < SEQ; t++) mask[t] = 1;
}

int main(void) {
#ifdef PIM_GEMV
    setup_paging();                     /* must precede any MMIO access */
#endif
    init_params();

    m5_reset_stats();                       /* ROI start: weight init excluded */
    for (int n = 0; n < ITERS; n++) bert_layer();
    m5_dump_stats();                        /* ROI end; no reset (see above)   */

    double sum = 0.0;
    for (int t = 0; t < SEQ; t++)
        for (int i = 0; i < HIDDEN; i++) sum += out[t][i];

    for (int i = 0; i < 4; i++) g_out[i] = out[0][i];
    g_checksum = sum;

#ifndef FREESTANDING
    printf("bert layer: hidden=%d heads=%d ffn=%d seq=%d iters=%d\n",
           HIDDEN, HEADS, FFN, SEQ, ITERS);
    printf("out[0][0..3] = %.6f %.6f %.6f %.6f\n",
           out[0][0], out[0][1], out[0][2], out[0][3]);
    printf("checksum     = %.6f\n", sum);
#elif defined(HAVE_IO)
    put_str("bert layer done\n");
    put_num("out[0][0]    = ", out[0][0]);
    put_num("out[0][1]    = ", out[0][1]);
    put_num("out[0][2]    = ", out[0][2]);
    put_num("out[0][3]    = ", out[0][3]);
    put_num("checksum     = ", sum);
#endif
#ifdef PIM_GEMV
    /* _start mret'd into main, so there is nothing to return to. */
    { volatile uint64_t c = handler_calls; (void)c; }   /* expect PIM_NDESC */
    m5_exit();
    for (;;) {}
#endif
    return (int)sum & 0xff;
}

#ifdef FREESTANDING
/* gem5 SE mode sets up sp and jumps here. gp must be initialised by hand:
 * -nostartfiles means there is no crt0, and GCC addresses small globals
 * relative to gp, so leaving it at 0 faults on the first such access.
 * norelax is required, or the assembler rewrites this very instruction into
 * the gp-relative form it is supposed to be setting up.
 * .bss is zero-filled by gem5, but nothing relies on that -- init_params
 * writes every array before it is read. */
__attribute__((naked, section(".text._start")))
void _start(void) {
    __asm__ volatile(
        ".option push\n\t"
        ".option norelax\n\t"
        "la    gp, __global_pointer$\n\t"
        ".option pop\n\t"
#ifdef BAREMETAL
        "la    sp, _stack_top\n\t"   /* no OS to set it up for us */
#endif
#ifdef PIM_GEMV
        /* Same sequence as tests/test-progs/pim_baremetal/start.S. gem5 starts
         * us in M-mode; the completion interrupt is handled in S-mode, so
         * delegate it, arm it, grant PMP, and mret down. */
        "li    t0, (1<<24)\n\t"
        "csrs  mideleg, t0\n\t"      /* local interrupt 24 -> S-mode */
        "la    t0, trap_handler\n\t"
        "csrw  stvec, t0\n\t"        /* where the doorbell goes */
        "li    t0, (1<<24)\n\t"
        "csrs  sie, t0\n\t"          /* enable that line */
        "csrsi sstatus, 0x2\n\t"     /* sstatus.SIE = 1 */
        "li    t0, -1\n\t"
        "csrw  pmpaddr0, t0\n\t"     /* S-mode is PMP-checked: no entry = */
        "li    t0, 0x1f\n\t"         /* deny all, so grant RWX everywhere */
        "csrw  pmpcfg0, t0\n\t"
        "li    t0, (3<<11)\n\t"
        "csrc  mstatus, t0\n\t"      /* MPP = 0 */
        "li    t0, (1<<11)\n\t"
        "csrs  mstatus, t0\n\t"      /* MPP = S */
        "la    t0, main\n\t"
        "csrw  mepc, t0\n\t"
        "mret\n\t"                   /* enter main in S-mode */
#else
        "call  main\n\t"
#endif
        "li    a0, 0\n\t"
        ".word 0x4200007B\n\t"      /* m5_exit */
        "1: j 1b\n\t"
    );
}
#endif
