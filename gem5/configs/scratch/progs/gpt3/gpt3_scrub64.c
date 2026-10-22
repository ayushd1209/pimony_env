
#include <stdint.h>

#ifdef FREESTANDING
#define K_INF __builtin_inff()
#else
#include <math.h>
#include <stdio.h>
#define K_INF INFINITY
#endif

/* ---------------- model configuration ---------------- */

/* GPT3-2.7B. Overridable so a tiny build can smoke-test mechanisms in seconds
 * instead of the ~100 min a full-size run costs. */
#ifndef HIDDEN
#define HIDDEN   2560             /* GPT3-2.7B d_model                        */
#endif
#ifndef HEADS
#define HEADS    32
#endif
#define HEAD_DIM (HIDDEN / HEADS) /* 80                                       */
#ifndef FFN
#define FFN      10240            /* d_interm                                 */
#endif
#ifndef SEQ
#define SEQ      1                /* tokens per pass. 1 = GEMV (no weight reuse,
                                   * decode-shaped). >1 = GEMM, which is how BERT
                                   * is actually run. Set with -DSEQ=n */
#endif
#ifndef CTX
#define CTX      256              /* cached past tokens. PIMony's input_len; grows
                                   * to 384 over output_len. Set with -DCTX=n   */
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
/* M5OP_CHECKPOINT 0x43 -> 0x43<<25 | 0x7B. Snapshots the machine so the 78 M
 * weights of init_params() are paid once instead of once per run. */
static inline void m5_checkpoint(void) {
    register uint64_t a0 asm("a0") = 0, a1 asm("a1") = 0;
    __asm__ volatile(".word 0x8600007B" ::"r"(a0), "r"(a1) : "memory");
}
#else
static inline void m5_reset_stats(void) {}
static inline void m5_dump_reset_stats(void) {}
static inline void m5_dump_stats(void) {}
static inline void m5_exit(void) {}
static inline void m5_checkpoint(void) {}
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
 *   Wq Wk Wv Wo W1 = PIM_PARTS(HIDDEN) each, W2 = PIM_PARTS(FFN).
 *   768/3072 -> 5*1 + 3 = 8;  2560/10240 -> 5*3 + 10 = 25 */
#define PIM_NDESC (5 * PIM_PARTS(HIDDEN) + PIM_PARTS(FFN))
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
/* max over BOTH matmul shapes. The old form hardcoded 1 part for the FFN side,
 * which is only true while PIM_PARTS(HIDDEN)==1; at 2560/10240 W1 needs
 * 3 x 10240 = 30720 and the device wrote 10 KB past the array. */
#define PIM_OUT_SLOTS (PIM_PARTS(HIDDEN) * FFN > PIM_PARTS(FFN) * HIDDEN \
                       ? PIM_PARTS(HIDDEN) * FFN : PIM_PARTS(FFN) * HIDDEN)
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

/* ---------------- the pim.dispatch comparison (-DPIM_BY_DISPATCH) -------------
 * The SAME matmul expanded by the HOST instead of by the sequencer: one
 * pim.dispatch per MAC command, 192 of them for a 768x768.
 *
 * Deliberately does LESS than pim.gemv, and the gap is the point:
 *   no descriptor fetch, no global-buffer fill (D2GWRITE), no readout (READRES),
 *   no result write-back. pim.dispatch cannot express any of them.
 * So this is pim.dispatch's BEST case. If it still loses, nobody can argue the
 * comparison was rigged.
 *
 * Address of command c = base of its first output, 4c, at input 0 -- the same
 * pim_idx() that laid the weights down, so the two agree by construction. */
#ifdef PIM_BY_DISPATCH

/* pim.dispatch a2, a0, a1 -> custom-0 funct3=3. comp bit (25) closes the group:
 * 0x00B5360B silent, 0x02B5360B reports completion and returns a token. */
static inline void pim_dispatch_mid(uint64_t addr, uint64_t num_macs) {
    register uint64_t a0 asm("a0") = addr, a1 asm("a1") = num_macs;
    register uint64_t a2 asm("a2");
    __asm__ volatile(".word 0x00B5360B" : "=r"(a2) : "r"(a0), "r"(a1)); (void)a2;
}
static inline uint64_t pim_dispatch_last(uint64_t addr, uint64_t num_macs) {
    register uint64_t a0 asm("a0") = addr, a1 asm("a1") = num_macs;
    register uint64_t a2 asm("a2");
    __asm__ volatile(".word 0x02B5360B" : "=r"(a2) : "r"(a0), "r"(a1)); return a2;
}

/* Breadth-first, exactly as the sequencer orders it: command c -> stream c%32.
 *
 * WAVE AT A TIME, and this is forced, not a handicap. A second MAC to a busy
 * bankgroup is a RESUME, not queued work, so firing all 192 aborts PIMony
 * ("PIM State: CLOSED, Command: MACINTR", measured 2026-09-19 after 143
 * commands). The sequencer avoids this by gating on engine_busy -- it sees every
 * completion. The host cannot see them. Its only tools are "there are 32
 * engines" and pim.wait, so the tightest SAFE schedule is 32 then wait.
 * That is pim.dispatch at its best; anything more aggressive crashes. */
/* Batch size. Three schedules were tried on the real matmul; only the last runs:
 *   192 (all at once)  -> PIMony ABORTS  ("PIM State: CLOSED, Command: MACINTR")
 *                         after 143 commands: a MAC to a busy bankgroup is a
 *                         RESUME, and the state machine ends up wrong.
 *    32 (one per engine)-> HANGS at 2,109 of 2,112. The host assumes all 32 are
 *                         free after a wait; it is wrong often enough to lose a
 *                         command, and the completion count never closes.
 *     1                -> works. No guessing: the engine that just finished is
 *                         the one about to be reused.
 * So 1 is not a handicap we chose, it is the fastest SAFE schedule available to
 * a host with no per-engine completion signal. Set -DPIM_WAVE=n to re-run the
 * failures. */
#ifndef PIM_WAVE
#define PIM_WAVE 1
#endif

static void pim_linear(int di, const wt_t *W, const float *b, float *y,
                       int out_dim, int in_dim) {
    (void)di;
    int parts = PIM_PARTS(in_dim);
    for (int p = 0; p < parts; p++) {
        int rem  = in_dim - p * PIM_MAX_IN;
        int len  = rem < PIM_MAX_IN ? rem : PIM_MAX_IN;
        uint64_t steps = len / WT_PER_STEP;
        uint64_t block = (uint64_t)PIM_WAVES(out_dim) * PIM_S_ROW;
        const wt_t *base = W + (uint64_t)p * block;
        int ncmd = (out_dim + 3) / 4;           /* one command drives 4 banks */

        for (int c0 = 0; c0 < ncmd; c0 += PIM_WAVE) {
            int end = c0 + PIM_WAVE < ncmd ? c0 + PIM_WAVE : ncmd;
            for (int c = c0; c < end - 1; c++)
                pim_dispatch_mid((uint64_t)(base + pim_idx(4 * c, 0)), steps);
            uint64_t tok = pim_dispatch_last(
                (uint64_t)(base + pim_idx(4 * (end - 1), 0)), steps);
            pim_wait(tok);                      /* drain before the next wave */
        }
    }
    for (int o = 0; o < out_dim; o++) y[o] = b[o];   /* no results to read back */
}

#else

/* Where a matmul's results land. Two matmuls in flight at once MUST NOT share a
 * region, or the second overwrites the first before the host has read it -- so
 * this is a requirement of overlap, not a convenience.
 * Wq/Wk/Wv are the only three ever in flight together (block 1), and they need
 * 3 x 2560 slots each = 23,040 of the 30,720 available. Every other matmul runs
 * alone and reuses region 0; W1 needs all 30,720 by itself.
 * di / PIM_PARTS(HIDDEN) is the matmul index, by the D_* spacing below. */
#define PIM_OUT_RGN_SLOTS ((unsigned long)PIM_PARTS(HIDDEN) * HIDDEN)
static wt_t *pim_out_region(int di) {
    int k = di / PIM_PARTS(HIDDEN);
    return pimout + (k < 3 ? (unsigned long)k * PIM_OUT_RGN_SLOTS : 0);
}

/* Read one finished slab back into y. Slab 0 seeds with the bias, later slabs
 * accumulate. Touches each slab once, unlike the old per-output loop over parts.
 * The device wrote DRAM behind the cache, so invalidate before reading -- and
 * the "memory" clobber in pim_fence_inv is what stops -O3 deleting all of this. */
static void pim_consume(const wt_t *out, int p, const float *b, float *y, int out_dim) {
    const wt_t *slab = out + (unsigned long)p * out_dim;
    pim_inv_range(slab, (unsigned long)out_dim * sizeof(wt_t));
    if (p == 0) for (int o = 0; o < out_dim; o++) y[o] = b[o] + (float)slab[o];
    else        for (int o = 0; o < out_dim; o++) y[o] +=        (float)slab[o];
}

/* One whole matmul on PIM, drop-in for linear(). The sequencer holds ONE job at
 * a time (memory_system.h GemvJob), so slabs cannot overlap each other -- but
 * the HOST can work inside a slab's window.
 *
 * A reduction longer than PIM_MAX_IN arrives as `parts` independent partial
 * arrays; summing them is the host's job. -DPIM_OVERLAP moves slab p-1's
 * read-back inside slab p's pim.wait, which is legal because the read-back
 * depends only on slab p-1. W2 has 10 slabs, so 9 of 10 read-backs move. */
/* THE ISSUE HALF. Runs every slab of one matmul and waits for each. Results are
 * left in this matmul's own output region, unread. Takes no y and no bias --
 * it produces nothing the host can see yet, which is exactly why the read half
 * can be deferred. */
static void pim_linear_issue(int di, const wt_t *W, int out_dim, int in_dim) {
    int parts = PIM_PARTS(in_dim);
    wt_t *out = pim_out_region(di);
    for (int p = 0; p < parts; p++) {
        uint64_t tok = pim_issue(di + p, W, out + (unsigned long)p * out_dim,
                                 out_dim, in_dim, p);
        pim_wait(tok);                     /* token stays in a register */
    }
}

/* THE READ HALF. Depends only on THIS matmul's region, so it is legal inside
 * any other matmul's pim.wait window. Unchanged from the original loop --
 * one pass per slab, accumulating into y. */
static void pim_linear_read(int di, const float *b, float *y,
                            int out_dim, int in_dim) {
    const wt_t *out = pim_out_region(di);
    int parts = PIM_PARTS(in_dim);

    pim_inv_range(out, (unsigned long)parts * out_dim * sizeof(wt_t));

    /* Output-major, as the original: the running sum stays in a register and y
     * is stored ONCE per output. Slab-major (a pass over y per slab) costs 23%
     * more instructions -- measured 2026-09-24 -- so only use it where a single
     * slab at a time is genuinely required. */
    for (int o = 0; o < out_dim; o++) {
        float acc = b[o];
        for (int p = 0; p < parts; p++)
            acc += (float)out[(unsigned long)p * out_dim + o];
        y[o] = acc;
    }
}

/* CROSS-MATMUL OVERLAP. Issue matmul B, and read matmul A's results inside B's
 * pim.wait windows. Legal because A's read touches only A's output region, and
 * B writes its own -- that is what the per-matmul regions above are for.
 *
 * Unlike the slab-major attempt, the read loop here is UNCHANGED: A's slabs are
 * consumed one per window in exactly the order and form they always were, so
 * there is no read-back penalty to claw back.
 *
 * A's read goes WHOLE into B's first window, not chopped up -- a window is
 * about 26,000 cycles and the read is about 24,000, so it should fit. If block
 * 1's sleep does not fall to near zero, it overflowed and the read needs
 * splitting by output range across the remaining windows. */
static void pim_linear_overlap(int di_b, const wt_t *Wb, int out_b, int in_b,
                               int di_a, const float *b_a, float *y_a,
                               int out_a, int in_a) {
    int parts_b = PIM_PARTS(in_b);
    wt_t *out_b_base = pim_out_region(di_b);

    for (int p = 0; p < parts_b; p++) {
        uint64_t tok = pim_issue(di_b + p, Wb, out_b_base + (unsigned long)p * out_b,
                                 out_b, in_b, p);
        if (p == 0)                                    /* host works, PIM runs */
            pim_linear_read(di_a, b_a, y_a, out_a, in_a);
        pim_wait(tok);                                 /* whichever is longer */
    }
}

static void pim_linear(int di, const wt_t *W, const float *b, float *y,
                       int out_dim, int in_dim) {
#ifdef PIM_OVERLAP
    /* slab-major interleave: read slab p-1 inside slab p's window. Measured
     * 2026-09-24 -- reclaims 28.9% of the sleep but the read-back it forces
     * costs more than it saves. Kept as the A/B control. */
    int parts = PIM_PARTS(in_dim);
    wt_t *out = pim_out_region(di);
    for (int p = 0; p < parts; p++) {
        uint64_t tok = pim_issue(di + p, W, out + (unsigned long)p * out_dim,
                                 out_dim, in_dim, p);
        if (p > 0) pim_consume(out, p - 1, b, y, out_dim); /* host works, PIM runs */
        pim_wait(tok);                                     /* whichever is longer */
    }
    pim_consume(out, parts - 1, b, y, out_dim);            /* no window left */
#else
    pim_linear_issue(di, W, out_dim, in_dim);
    pim_linear_read(di, b, y, out_dim, in_dim);
#endif
}

#endif  /* PIM_BY_DISPATCH */

/* Descriptor slots, PIM_PARTS(HIDDEN) apart: pim_issue uses di+p, so each
 * matmul owns a contiguous run and no two overlap. */
#define D_WQ (0 * PIM_PARTS(HIDDEN))
#define D_WK (1 * PIM_PARTS(HIDDEN))
#define D_WV (2 * PIM_PARTS(HIDDEN))
#define D_WO (3 * PIM_PARTS(HIDDEN))
#define D_W1 (4 * PIM_PARTS(HIDDEN))
#define D_W2 (5 * PIM_PARTS(HIDDEN))

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

/* Sv39 identity paging: one gigapage for DRAM, one for the PIM MMIO window.
 * Preloaded in the image: the walker reads DRAM directly, and on Minor it walks
 * before the stores below have left the store buffer (instruction page fault). */
#define GIGAPAGE(pa) ((((uint64_t)(pa) >> 12) << 10) | 0xCF)
static uint64_t root_table[512] __attribute__((aligned(4096))) = {
    [2] = GIGAPAGE(0x80000000ULL), [4] = GIGAPAGE(0x100000000ULL),
};
static uint64_t make_gigapage(uint64_t pa) { return GIGAPAGE(pa); }
static void setup_paging(void) {
    root_table[2] = make_gigapage(0x80000000ULL);    /* same values; kept so .text is unchanged */
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
static float Kc[CTX + 1][HIDDEN];              /* KV cache + this step */
static float Vc[CTX + 1][HIDDEN];
static float att[HEADS][CTX + 1];              /* scores, then weights */

/* -DPIM_ATT_HEADS=N: heads [0, N) run attention on the PIM, [N, HEADS) on the CPU. */
#ifndef PIM_ATT_HEADS
#define PIM_ATT_HEADS 0
#endif
#if PIM_ATT_HEADS > 0
#if !defined(PIM_GEMV) || !defined(PIM_LAYOUT)
#error "PIM_ATT_HEADS needs PIM_GEMV and PIM_LAYOUT"
#endif
#ifdef PIM_OVERLAP_ATT
#error "PIM_ATT_HEADS does not support PIM_OVERLAP_ATT"
#endif
#if PIM_ATT_HEADS > HEADS || CTX + 1 > PIM_MAX_IN
#error "PIM_ATT_HEADS > HEADS, or CTX+1 does not fit one row"
#endif
/* Per-head KV copies in PIM layout, FP16. Each head is whole rows, so every base is 256 KB aligned. */
#define PIM_K_ELEMS (PIM_WAVES(CTX + 1) * PIM_S_ROW)  /* CTX+1 outputs x HEAD_DIM: 3 rows, 768 KB */
#define PIM_V_ELEMS (PIM_WAVES(HEAD_DIM) * PIM_S_ROW) /* HEAD_DIM outputs x CTX+1: 1 row, 256 KB */
/* Results come in groups of 4: CTX+1 outputs write 4*ceil((CTX+1)/4), padded to whole lines. */
#define PIM_ATT_OUT ((4 * ((CTX + 4) / 4) + 31) / 32 * 32)  /* 260 -> 288 */
/* Fixed region above the image, not .bss: new .bss arrays moved Kc/Vc and cut the CPU heads'
 * prefetch coverage (s2 +18%). Here every existing array keeps its address. */
#define PIM_ATT_BASE 0x90000000ULL  /* 256 KB aligned; image ends ~0x8B400000, DRAM + gigapage to 0xC0000000 */
#define pimK   ((wt_t (*)[PIM_K_ELEMS])PIM_ATT_BASE)                             /* (t, d) at pim_idx(t, d) */
#define pimV   ((wt_t (*)[PIM_V_ELEMS])(PIM_ATT_BASE + 2ULL * PIM_ATT_HEADS * PIM_K_ELEMS))  /* (d, t) */
#define attvec ((wt_t *)(PIM_ATT_BASE + 2ULL * PIM_ATT_HEADS * (PIM_K_ELEMS + PIM_V_ELEMS)))  /* one full row */
#define attout ((wt_t *)((uint64_t)attvec + 2ULL * PIM_STEPS_ROW * PIM_S_STEP))   /* 64 B aligned, own lines */
#define g_att_desc ((struct pim_gemv_desc *)((uint64_t)attout + 2ULL * PIM_ATT_OUT))  /* 0 = scores, 1 = context */
#endif
static float ctx[SEQ][HIDDEN];                 /* heads concatenated   */
static float proj[SEQ][HIDDEN] PIM_OUT_ALIGN;                /* attention output     */
static float norm1[SEQ][HIDDEN];
static float hid[SEQ][FFN] PIM_OUT_ALIGN;                    /* FFN intermediate     */
static float ff[SEQ][HIDDEN] PIM_OUT_ALIGN;
static float out[SEQ][HIDDEN];

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

/* -nostdlib has no libc, but GCC turns zeroing loops into memset calls.
 * no-tree-loop-distribute-patterns stops this one calling itself. */
__attribute__((used, optimize("no-tree-loop-distribute-patterns")))
void *memset(void *d, int c, unsigned long n) {
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

/* OpenBLAS GEMV kernels, linked straight in. cblas_sgemv pulls blas_memory_alloc
 * and __assert_fail; the kernels underneath have NO undefined symbols, so they
 * live in a freestanding bare-metal binary. Verified in blastest/.
 * Column-major with lda=HIDDEN makes the row-major head block A[i][j]=Kc[j][off+i],
 * i.e. HEAD_DIM x L -- so scores are A^T q (sgemv_t) and context is A att (sgemv_n).
 * Both accumulate into y, so y is zeroed first. */
#ifndef ATT_SCALAR
int sgemv_n(long, long, long, float, float *, long, float *, long, float *, long, float *);
int sgemv_t(long, long, long, float, float *, long, float *, long, float *, long, float *);
static float blas_buf[4096];
#endif

/* Decode-step attention, split into its two halves. Steps 1-3 (scores, scale,
 * softmax) need Q and K. Step 4 (context) needs V. PIM produces Q, K and V in
 * that order, so the first half is legal while Wv is still running and the
 * second half is not -- that split is the whole point of the division below.
 *
 * Causal masking is implicit: everything in the cache is already in the past.
 * Head h owns columns [h*HEAD_DIM, +HEAD_DIM). */

static void attention_append_k(void) {     /* needs K */
    for (int d = PIM_ATT_HEADS * HEAD_DIM; d < HIDDEN; d++) Kc[CTX][d] = K[0][d];
}
static void attention_append_v(void) {     /* needs V */
    for (int d = PIM_ATT_HEADS * HEAD_DIM; d < HIDDEN; d++) Vc[CTX][d] = V[0][d];
}

#if PIM_ATT_HEADS > 0
/* PIM heads: new token into slot CTX of their copies, then clean each touched line once. */
static void pim_att_append(void) {
    for (int h = 0; h < PIM_ATT_HEADS; h++) {
        for (int d = 0; d < HEAD_DIM; d++) {
            pimK[h][pim_idx(CTX, d)] = (wt_t)K[0][h * HEAD_DIM + d];
            pimV[h][pim_idx(d, CTX)] = (wt_t)V[0][h * HEAD_DIM + d];
        }
        for (int d = 0; d < HEAD_DIM; d += WT_PER_STEP)   /* K: one 32 B step per line, 5 lines */
            pim_fence_cl(&pimK[h][pim_idx(CTX, d)]);
        for (int d = 0; d < HEAD_DIM; d++)                /* V: ch 0+1 and 2+3 share a line, 40 lines */
            if ((d / 4) % 2 == 0) pim_fence_cl(&pimV[h][pim_idx(d, CTX)]);
    }
}

/* Init, outside the ROI: copies of Kc/Vc rows 0..CTX-1, then pushed to DRAM. */
static void pim_att_init(void) {
    for (int h = 0; h < PIM_ATT_HEADS; h++) {
        for (int t = 0; t < CTX; t++)
            for (int d = 0; d < HEAD_DIM; d++) {
                pimK[h][pim_idx(t, d)] = (wt_t)Kc[t][h * HEAD_DIM + d];
                pimV[h][pim_idx(d, t)] = (wt_t)Vc[t][h * HEAD_DIM + d];
            }
        for (uint64_t i = 0; i < PIM_K_ELEMS; i += 32) pim_fence_cl(&pimK[h][i]);  /* 32 wt_t = 64 B */
        for (uint64_t i = 0; i < PIM_V_ELEMS; i += 32) pim_fence_cl(&pimV[h][i]);
    }
}

/* Stage n values, zero-padded to whole steps, into all 4 channel copies; clean. Returns dot steps. */
static uint32_t pim_att_stage(const float *v, int n) {
    int steps = (n + WT_PER_STEP - 1) / WT_PER_STEP;
    for (int i = 0; i < steps * WT_PER_STEP; i++) {
        wt_t e = i < n ? (wt_t)v[i] : (wt_t)0;           /* zero tail: no stale inputs */
        for (int ch = 0; ch < 4; ch++) attvec[pim_vec_idx(ch, i)] = e;
    }
    for (int s = 0; s < steps; s++) {                    /* 4 copies x 32 B = 2 lines per step */
        const char *p = (const char *)&attvec[(uint64_t)s * PIM_S_STEP];
        pim_fence_cl(p);
        pim_fence_cl(p + 64);
    }
    return (uint32_t)steps;
}

/* pim_issue() with explicit dot steps: 257 inputs need 17, len / WT_PER_STEP gives 16. */
static uint64_t pim_att_issue(int di, const wt_t *W, uint32_t outputs, uint32_t steps) {
    g_att_desc[di].w_base = (uint64_t)W;
    g_att_desc[di].v_base = (uint64_t)attvec;
    g_att_desc[di].out_base = (uint64_t)attout;
    pim_fence_cl(&g_att_desc[di]);                       /* descriptor -> DRAM */
    return pim_gemv(&g_att_desc[di], outputs, steps);
}

/* One head on the PIM: scores = K q, scale + softmax on the CPU, context = V^T w. */
static void pim_att_head(int h) {
    const float scale = 1.0f / k_sqrtf((float)HEAD_DIM);
    const int L = CTX + 1;
    const int off = h * HEAD_DIM;
    float m, sum = 0.0f;

    pim_wait(pim_att_issue(0, pimK[h], L, pim_att_stage(&Q[0][off], HEAD_DIM)));
    pim_inv_range(attout, (unsigned long)L * sizeof(wt_t));
    for (int j = 0; j < L; j++) att[h][j] = (float)attout[j] * scale;   /* CPU path: sgemv_t alpha */

    m = att[h][0];                         /* softmax, copied from attention_scores() */
    for (int j = 1; j < L; j++) if (att[h][j] > m) m = att[h][j];
    for (int j = 0; j < L; j++) { att[h][j] = k_expf(att[h][j] - m); sum += att[h][j]; }
    for (int j = 0; j < L; j++) att[h][j] /= sum;

    pim_wait(pim_att_issue(1, pimV[h], HEAD_DIM, pim_att_stage(&att[h][0], L)));
    pim_inv_range(attout, (unsigned long)HEAD_DIM * sizeof(wt_t));
    for (int d = 0; d < HEAD_DIM; d++) ctx[0][off + d] = (float)attout[d];
}
#endif

/* Steps 1-3 for heads [h0, h1). Leaves normalised weights in att[h][]. */
static void attention_scores(int h0, int h1) {
    const float scale = 1.0f / k_sqrtf((float)HEAD_DIM);
    const int L = CTX + 1;

    for (int h = h0; h < h1; h++) {
        const int off = h * HEAD_DIM;
        float m, sum = 0.0f;

        for (int j = 0; j < L; j++) att[h][j] = 0.0f;
        sgemv_t(HEAD_DIM, L, 0, scale, &Kc[0][off], HIDDEN,
                &Q[0][off], 1, &att[h][0], 1, blas_buf);

        m = att[h][0];                     /* softmax, max-subtracted */
        for (int j = 1; j < L; j++) if (att[h][j] > m) m = att[h][j];
        for (int j = 0; j < L; j++) { att[h][j] = k_expf(att[h][j] - m); sum += att[h][j]; }
        for (int j = 0; j < L; j++) att[h][j] /= sum;
    }
}

/* Step 4 for the CPU heads [PIM_ATT_HEADS, HEADS): ctx = weights . Vc. Constant range, not
 * parameters: (h0, h1) changed the N=0 binary's register allocation. */
static void attention_context(void) {
    const int L = CTX + 1;
    for (int h = PIM_ATT_HEADS; h < HEADS; h++) {
        const int off = h * HEAD_DIM;
        for (int d = 0; d < HEAD_DIM; d++) ctx[0][off + d] = 0.0f;
        sgemv_n(HEAD_DIM, L, 0, 1.0f, &Vc[0][off], HIDDEN,
                &att[h][0], 1, &ctx[0][off], 1, blas_buf);
    }
}

/* The whole thing, in order -- what block 2 calls when nothing is overlapped. */
static void attention(void) {
    attention_append_k();
    attention_append_v();
#if PIM_ATT_HEADS > 0
    pim_att_append();
    for (int h = 0; h < PIM_ATT_HEADS; h++) pim_att_head(h);
#endif
    attention_scores(PIM_ATT_HEADS, HEADS);
    attention_context();
}

/* ---------------- the layer ---------------- */

/* GPT-3 block: PRE-layernorm. BERT normalises after each sub-block, GPT before
 * it, so the norms move up and the residual adds are bare. Section numbering is
 * unchanged -- blocks 1/3/5/7 are still the matmuls. */
static void bert_layer(void) {
    /* attention block. norm1 = LN(x) feeds Q/K/V, so one store serves all three. */
    layernorm(&x[0][0], g1, beta1, &norm1[0][0]);
    PIM_STORE_VEC(&norm1[0][0], HIDDEN);
    PIM_FLUSH_VEC(HIDDEN);                 /* one flush serves Q/K/V */
#if defined(PIM_GEMV) && defined(PIM_OVERLAP_ATT)
    /* As PIM_OVERLAP_MM, plus the SCORE HALF of attention inside Wv's windows.
     * Steps 1-3 need Q and K only, both finished before Wv starts; step 4 needs
     * V and stays in block 2. The read-back was worth ~32k cycles; the score
     * half is worth ~1M against 214k of sleep, so this one can fill the window.
     *
     * NOTE FOR THE METRIC: the scores move OUT of block 2 and INTO block 1, so
     * "matmul-only" is no longer a clean category here. Compare blocks 1+2
     * against the baseline's blocks 1+2. */
    pim_linear_issue(D_WQ, &Wq[0][0], HIDDEN, HIDDEN);
    pim_linear_overlap(D_WK, &Wk[0][0], HIDDEN, HIDDEN,
                       D_WQ, bq, &Q[0][0], HIDDEN, HIDDEN);   /* read Q under K */
    {
        int parts = PIM_PARTS(HIDDEN);
        wt_t *out = pim_out_region(D_WV);
        for (int p = 0; p < parts; p++) {
            uint64_t tok = pim_issue(D_WV + p, &Wv[0][0],
                                     out + (unsigned long)p * HIDDEN,
                                     HIDDEN, HIDDEN, p);
            if (p == 0) {                       /* K must land before any score */
                pim_linear_read(D_WK, bk, &K[0][0], HIDDEN, HIDDEN);
                attention_append_k();
            }
            attention_scores(p * HEADS / parts, (p + 1) * HEADS / parts);
            pim_wait(tok);
        }
    }
    pim_linear_read(D_WV, bv, &V[0][0], HIDDEN, HIDDEN);      /* no window left */
    attention_append_v();
#elif defined(PIM_GEMV) && defined(PIM_OVERLAP_MM)
    /* Q, K and V are three INDEPENDENT matmuls, so each one's read-back is
     * legal inside the next one's wait windows. Only V's read has no window
     * left after it. This is the only block in the layer with that structure:
     * Wo, W1 and W2 each consume the output of the code immediately before
     * them, so nothing outside them is ever independent. */
    pim_linear_issue(D_WQ, &Wq[0][0], HIDDEN, HIDDEN);
    pim_linear_overlap(D_WK, &Wk[0][0], HIDDEN, HIDDEN,
                       D_WQ, bq, &Q[0][0], HIDDEN, HIDDEN);   /* read Q under K */
    pim_linear_overlap(D_WV, &Wv[0][0], HIDDEN, HIDDEN,
                       D_WK, bk, &K[0][0], HIDDEN, HIDDEN);   /* read K under V */
    pim_linear_read(D_WV, bv, &V[0][0], HIDDEN, HIDDEN);      /* no window left */
#else
    LINEAR(D_WQ, Wq, bq, &norm1[0][0], &Q[0][0], HIDDEN, HIDDEN);
    LINEAR(D_WK, Wk, bk, &norm1[0][0], &K[0][0], HIDDEN, HIDDEN);
    LINEAR(D_WV, Wv, bv, &norm1[0][0], &V[0][0], HIDDEN, HIDDEN);
#endif
    PHASE();                               /* [1] LN 1 + QKV                 */

#if defined(PIM_GEMV) && defined(PIM_OVERLAP_ATT)
    attention_context();                   /* steps 1-3 already ran in block 1 */
#else
    attention();                           /* stays on the CPU by design */
#endif
    PHASE();                               /* [2] attention                  */

    PIM_STORE_VEC(&ctx[0][0], HIDDEN);
    PIM_FLUSH_VEC(HIDDEN);
    LINEAR(D_WO, Wo, bo, &ctx[0][0], &proj[0][0], HIDDEN, HIDDEN);
    PHASE();                               /* [3] Wo                         */

    /* residual, then the FFN's own pre-norm */
    residual_add(&x[0][0], &proj[0][0], &proj[0][0], SEQ * HIDDEN);
    layernorm(&proj[0][0], g2, beta2, &norm1[0][0]);
    PHASE();                               /* [4] residual 1 + LN 2          */

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

    /* residual only -- pre-LN puts the final norm at the end of the stack,
     * not in the block. */
    residual_add(&proj[0][0], &ff[0][0], &out[0][0], SEQ * HIDDEN);
    PHASE();                               /* [8] residual 2                 */
}

/* -DCACHE_SCRUB: 4 MB of reads above the image (2x the LRU L2), so no program data is cached at ROI start.
 * Fixed-size asm, exactly 64 B and no registers taken from GCC: main's ROI code moves by 64 B, layout unchanged. */
#ifdef CACHE_SCRUB
static inline void cache_scrub(void) {
    __asm__ volatile(
        ".option push\n\t.option norvc\n\t"
        "addi sp, sp, -32\n\t"
        "sd   t0, 0(sp)\n\t"
        "sd   t1, 8(sp)\n\t"
        "sd   t2, 16(sp)\n\t"
        "lui  t0, 0x4c000\n\t"
        "slli t0, t0, 1\n\t"                     /* t0 = 0x98000000, past the PIM region */
        "lui  t1, 0x400\n\t"
        "add  t1, t1, t0\n\t"                    /* t1 = end, +4 MB */
        "1: lbu t2, 0(t0)\n\t"                   /* one load per 64 B line */
        "addi t0, t0, 64\n\t"
        "bltu t0, t1, 1b\n\t"
        "ld   t0, 0(sp)\n\t"
        "ld   t1, 8(sp)\n\t"
        "ld   t2, 16(sp)\n\t"
        "addi sp, sp, 32\n\t"
        "nop\n\t"                                /* pad to 16 insts = 64 B */
        ".option pop"
        ::: "memory");
}
#endif

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
    fill(&Kc[0][0], (uint64_t)CTX * HIDDEN, sh);      /* CTX tokens of history */
    fill(&Vc[0][0], (uint64_t)CTX * HIDDEN, sh);
#if PIM_ATT_HEADS > 0
    pim_att_init();                                   /* written last: clean before the ROI */
#endif
#ifdef CACHE_SCRUB
    cache_scrub();                                    /* very last: no program data cached at ROI start */
#endif
}

int main(void) {
#ifdef PIM_GEMV
    setup_paging();                     /* must precede any MMIO access */
#endif
    init_params();

#ifdef TAKE_CKPT
    m5_checkpoint();       /* snapshot here; the config saves and exits */
#endif
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
