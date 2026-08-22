
#include <stdint.h>

#ifdef FREESTANDING
#define K_INF __builtin_inff()
#else
#include <math.h>
#include <stdio.h>
#define K_INF INFINITY
#endif

#ifdef USE_BLAS                   /* hosted only -- needs libc */
#include <cblas.h>
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
static inline void m5_exit(void) {
    register uint64_t a0 asm("a0") = 0;
    __asm__ volatile(".word 0x4200007B" ::"r"(a0) : "memory");
}
#else
static inline void m5_reset_stats(void) {}
static inline void m5_dump_reset_stats(void) {}
static inline void m5_exit(void) {}
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

/* ---------------- parameters (~28 MB, lives in .bss) ---------------- */

static float Wq[HIDDEN][HIDDEN], bq[HIDDEN];
static float Wk[HIDDEN][HIDDEN], bk[HIDDEN];
static float Wv[HIDDEN][HIDDEN], bv[HIDDEN];
static float Wo[HIDDEN][HIDDEN], bo[HIDDEN];
static float W1[FFN][HIDDEN],    b1[FFN];      /* expand  768 -> 3072 */
static float W2[HIDDEN][FFN],    b2[HIDDEN];   /* project 3072 -> 768 */
static float g1[HIDDEN], beta1[HIDDEN];        /* norm after attention */
static float g2[HIDDEN], beta2[HIDDEN];        /* norm after FFN       */

/* ---------------- activations ---------------- */

static float x[SEQ][HIDDEN];                   /* layer input          */
static float Q[SEQ][HIDDEN], K[SEQ][HIDDEN], V[SEQ][HIDDEN];
static float att[HEADS][SEQ][SEQ];             /* scores, then weights */
static float ctx[SEQ][HIDDEN];                 /* heads concatenated   */
static float proj[SEQ][HIDDEN];                /* attention output     */
static float norm1[SEQ][HIDDEN];
static float hid[SEQ][FFN];                    /* FFN intermediate     */
static float ff[SEQ][HIDDEN];
static float out[SEQ][HIDDEN];
static int   mask[SEQ];                        /* 1 = attend, 0 = ignore */

/* results, volatile so they survive -O3 and can be read from a memory dump */
volatile float  g_out[4];
volatile double g_checksum;

/* ---------------- kernels ---------------- */

#ifdef USE_BLAS
/* Same contract as the hand-written linear() below, expressed as BLAS.
 * y[t][o] = b[o] + sum_i W[o][i]*in[t][i]  ==  C = in * W^T with C seeded to b.
 * SEQ is a compile-time constant, so the branch folds away. */
static void linear(const float *W, const float *b, const float *in, float *y,
                   int n_out, int n_in) {
    for (int t = 0; t < SEQ; t++)                      /* seed C with bias */
        for (int o = 0; o < n_out; o++) y[(uint64_t)t * n_out + o] = b[o];

    if (SEQ == 1)                                      /* GEMV: A[n_out][n_in]*x */
        cblas_sgemv(CblasRowMajor, CblasNoTrans, n_out, n_in,
                    1.0f, W, n_in, in, 1, 1.0f, y, 1);
    else                                               /* GEMM: C[SEQ][n_out] */
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                    SEQ, n_out, n_in, 1.0f, in, n_in, W, n_in, 1.0f, y, n_out);
}
#elif defined(ACC8)
/* Same result as the loop below, but with EIGHT independent running totals
 * instead of one. Each FMA now waits only on its own accumulator, so the
 * machine can keep several in flight rather than stalling 5 cycles per MAC.
 * n_in is 768 or 3072, both multiples of 8, so the tail loop never runs. */
static void linear(const float *W, const float *b, const float *in, float *y,
                   int n_out, int n_in) {
    for (int o = 0; o < n_out; o++) {
        const float *w = W + (uint64_t)o * n_in;
        for (int t = 0; t < SEQ; t++) {
            const float *v = in + (uint64_t)t * n_in;
            float s0=0.f, s1=0.f, s2=0.f, s3=0.f, s4=0.f, s5=0.f, s6=0.f, s7=0.f;
            int i = 0;
            for (; i + 7 < n_in; i += 8) {
                s0 += w[i  ] * v[i  ];  s1 += w[i+1] * v[i+1];
                s2 += w[i+2] * v[i+2];  s3 += w[i+3] * v[i+3];
                s4 += w[i+4] * v[i+4];  s5 += w[i+5] * v[i+5];
                s6 += w[i+6] * v[i+6];  s7 += w[i+7] * v[i+7];
            }
            float s = b[o];
            for (; i < n_in; i++) s += w[i] * v[i];            /* tail */
            y[(uint64_t)t * n_out + o] =
                s + (((s0+s1)+(s2+s3)) + ((s4+s5)+(s6+s7)));   /* tree reduce */
        }
    }
}
#else
/* y[t][o] = b[o] + sum_i W[o][i] * in[t][i]
 * THE hot loop: >99% of this layer's multiply-accumulates land here, and the
 * inner loop is contiguous in both operands. This is the PIM offload target. */
static void linear(const float *W, const float *b, const float *in, float *y,
                   int n_out, int n_in) {
    /* Output loop OUTERMOST, token loop inside: each weight row is fetched once
     * and consumed by all SEQ tokens while it is still in L1. With the loops the
     * other way round the whole 28 MB of weights is re-walked per token, so
     * weight traffic scales with SEQ and no reuse is captured at all.
     * At SEQ=1 the two orders are identical. */
    for (int o = 0; o < n_out; o++) {
        const float *w = W + (uint64_t)o * n_in;
        for (int t = 0; t < SEQ; t++) {
            const float *v = in + (uint64_t)t * n_in;
            float s = b[o];
            for (int i = 0; i < n_in; i++) s += w[i] * v[i];
            y[(uint64_t)t * n_out + o] = s;
        }
    }
}
#endif  /* USE_BLAS */

#ifdef USE_BLAS
/* OpenBLAS mmaps its packing buffers on the first BLAS call. Spend that once
 * on a tiny slice before the ROI opens; bert_layer() overwrites Q completely,
 * so this cannot perturb the result. */
static void blas_warmup(void) {
    linear(&Wq[0][0], bq, &x[0][0], &Q[0][0], 8, HIDDEN);
}
#else
#define blas_warmup() ((void)0)
#endif

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
    /* self-attention block */
    linear(&Wq[0][0], bq, &x[0][0], &Q[0][0], HIDDEN, HIDDEN);
    linear(&Wk[0][0], bk, &x[0][0], &K[0][0], HIDDEN, HIDDEN);
    linear(&Wv[0][0], bv, &x[0][0], &V[0][0], HIDDEN, HIDDEN);

    attention();
    linear(&Wo[0][0], bo, &ctx[0][0], &proj[0][0], HIDDEN, HIDDEN);

    /* residual + norm */
    residual_add(&x[0][0], &proj[0][0], &proj[0][0], SEQ * HIDDEN);
    layernorm(&proj[0][0], g1, beta1, &norm1[0][0]);

    /* feed-forward block */
    linear(&W1[0][0], b1, &norm1[0][0], &hid[0][0], FFN, HIDDEN);
    for (int i = 0; i < SEQ * FFN; i++) (&hid[0][0])[i] = gelu((&hid[0][0])[i]);
    linear(&W2[0][0], b2, &hid[0][0], &ff[0][0], HIDDEN, FFN);

    /* residual + norm */
    residual_add(&norm1[0][0], &ff[0][0], &ff[0][0], SEQ * HIDDEN);
    layernorm(&ff[0][0], g2, beta2, &out[0][0]);
}

static void init_params(void) {
    const float sh = 1.0f / k_sqrtf((float)HIDDEN);   /* fan_in = 768  */
    const float sf = 1.0f / k_sqrtf((float)FFN);      /* fan_in = 3072 */

    rng_state = SEED;

    fill(&Wq[0][0], (uint64_t)HIDDEN * HIDDEN, sh); fill(bq, HIDDEN, sh);
    fill(&Wk[0][0], (uint64_t)HIDDEN * HIDDEN, sh); fill(bk, HIDDEN, sh);
    fill(&Wv[0][0], (uint64_t)HIDDEN * HIDDEN, sh); fill(bv, HIDDEN, sh);
    fill(&Wo[0][0], (uint64_t)HIDDEN * HIDDEN, sh); fill(bo, HIDDEN, sh);

    fill(&W1[0][0], (uint64_t)FFN * HIDDEN, sh);    fill(b1, FFN, sh);
    fill(&W2[0][0], (uint64_t)HIDDEN * FFN, sf);    fill(b2, HIDDEN, sf);

    fill(g1, HIDDEN, 0.1f); for (int i = 0; i < HIDDEN; i++) g1[i] += 1.0f;
    fill(beta1, HIDDEN, 0.1f);
    fill(g2, HIDDEN, 0.1f); for (int i = 0; i < HIDDEN; i++) g2[i] += 1.0f;
    fill(beta2, HIDDEN, 0.1f);

    fill(&x[0][0], (uint64_t)SEQ * HIDDEN, 1.0f);     /* layer input */
    for (int t = 0; t < SEQ; t++) mask[t] = 1;
}

int main(void) {
    init_params();

    blas_warmup();                          /* allocation kept out of the ROI */
    m5_reset_stats();                       /* ROI start: weight init excluded */
    for (int n = 0; n < ITERS; n++) bert_layer();
    m5_dump_reset_stats();                  /* ROI end                         */

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
        "call  main\n\t"
        "li    a0, 0\n\t"
        ".word 0x4200007B\n\t"      /* m5_exit */
        "1: j 1b\n\t"
    );
}
#endif
