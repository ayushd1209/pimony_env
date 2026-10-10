/* GPT3-2.7B layer, PREFILL, host baseline: SEQ prompt tokens in one pass,
 * empty KV cache. A copy of gpt3_blas.c (decode) with only attention changed:
 * each token attends to itself and the tokens before it (causal).
 * gpt3_blas.c stays untouched, so its runs stay reproducible.
 *
 * Build (gpt3_blas_c's flags, plus -DSEQ):
 *   /opt/riscv/bin/riscv64-unknown-linux-gnu-gcc -O3 -march=rv64gcv_zfh \
 *     -mabi=lp64d -mcmodel=medany -static -DBLAS_FP16 -DPHASE_STATS -DSEQ=256 \
 *     -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math \
 *     -I/opt/openblas-zvl256b/include -o gpt3_prefill_s256 gpt3_prefill.c \
 *     /opt/openblas-zvl256b/lib/libopenblas.a -lm
 *
 * Run:
 *   build/RISCV/gem5.opt -d m5out/gpt3_prefill_s256_o3 configs/pimony/se_bert.py \
 *       o3 configs/scratch/progs/gpt3/gpt3_prefill_s256
 *
 * Correctness check: add -DCHECK_ATTN (compares attention against a plain
 * reference after the ROI); -DHIDDEN/-DHEADS/-DFFN shrink it to run in seconds.
 */

#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <cblas.h>

/* ---------------- model configuration ---------------- */

#ifndef HIDDEN                    /* overridable for a tiny check build       */
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
#define SEQ      256              /* prompt tokens, one pass. 256 = PIMony's
                                   * gpt3-2.7B input length. Set with -DSEQ=n */
#endif
#define ITERS    1
#define LN_EPS   1e-5f            /* torch.nn.LayerNorm default               */

/* ---------------- gem5 stats markers ----------------
 * a0=delay a1=period, both 0 = dump once, now. No-ops off RISC-V so the same
 * source builds natively for validation. */

#ifdef __riscv
static inline void m5_reset_stats(void) {
    register uint64_t a0 asm("a0") = 0, a1 asm("a1") = 0;
    __asm__ volatile(".word 0x8000007B" ::"r"(a0), "r"(a1) : "memory");
}
static inline void m5_dump_reset_stats(void) {
    register uint64_t a0 asm("a0") = 0, a1 asm("a1") = 0;
    __asm__ volatile(".word 0x8400007B" ::"r"(a0), "r"(a1) : "memory");
}
#else
static inline void m5_reset_stats(void) {}
static inline void m5_dump_reset_stats(void) {}
#endif

/* Per-section timing, -DPHASE_STATS. Each call dumps and resets, so stats block
 * N is section N of bert_layer(). Off by default: the plain build is unchanged. */
#ifdef PHASE_STATS
#define PHASE() m5_dump_reset_stats()
#else
#define PHASE() ((void)0)
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
/* Weight element type. -DBLAS_FP16 runs the six projections in half precision
 * through cblas_shgemm, which is what the PIM path stores and moves; without it
 * the file is bit-identical to before. hfloat16 is OpenBLAS's own name for
 * _Float16 (openblas_config.h), the same type bert.c's wt_t uses.
 * shgemv IS vectorised on this target -- KERNEL.RISCV64_ZVL256B sets
 * SHGEMVTKERNEL = sbgemv_t_vector.c, and the built shgemv_t.o disassembles to
 * vle16/vfwmul/vfredusum. OpenBLAS just never declares it in cblas.h, so the
 * prototype is written out below. Types from interface/sbgemv.c: IFLOAT (the
 * half type) for the matrix and vector, FLOAT (= float under HFLOAT16) for
 * alpha, beta and the output. */
#ifdef BLAS_FP16
/* openblas_config.h falls back to `typedef uint16_t hfloat16` when the compiler
 * cannot do _Float16 for this -march. That fallback COMPILES AND RUNS, and
 * produces silent garbage -- every (wt_t) cast becomes an integer truncation.
 * Fail the build instead. */
#ifndef __FLT16_MAX__
#error "BLAS_FP16 needs real _Float16 support: add zfh to -march, or hfloat16 silently becomes uint16_t"
#endif
typedef hfloat16 wt_t;
void cblas_shgemv(enum CBLAS_ORDER order, enum CBLAS_TRANSPOSE TransA,
                  blasint m, blasint n, float alpha,
                  hfloat16 *a, blasint lda, hfloat16 *x, blasint incx,
                  float beta, float *y, blasint incy);
#else
typedef float wt_t;
#endif

static float rng_uniform(void) {
    return (float)(rng_u32() >> 8) / 8388608.0f * 2.0f - 1.0f;
}

/* Same draw order as fill(), so switching precision changes how the weights are
 * STORED, never which random numbers they are. */
static void fill_w(wt_t *p, uint64_t n, float scale) {
    for (uint64_t i = 0; i < n; i++) p[i] = (wt_t)(rng_uniform() * scale);
}

static void fill(float *p, uint64_t n, float scale) {
    for (uint64_t i = 0; i < n; i++) p[i] = rng_uniform() * scale;
}

/* ---------------- parameters (~28 MB, lives in .bss) ---------------- */

/* weights are wt_t (FP16 under -DBLAS_FP16); biases stay FP32 -- shgemm's
 * output and accumulation are FP32, so the bias seeding C is FP32 too. */
static wt_t  Wq[HIDDEN][HIDDEN]; static float bq[HIDDEN];
static wt_t  Wk[HIDDEN][HIDDEN]; static float bk[HIDDEN];
static wt_t  Wv[HIDDEN][HIDDEN]; static float bv[HIDDEN];
static wt_t  Wo[HIDDEN][HIDDEN]; static float bo[HIDDEN];
static wt_t  W1[FFN][HIDDEN];    static float b1[FFN];      /* expand  768 -> 3072 */
static wt_t  W2[HIDDEN][FFN];    static float b2[HIDDEN];   /* project 3072 -> 768 */
static float g1[HIDDEN], beta1[HIDDEN];        /* norm after attention */
static float g2[HIDDEN], beta2[HIDDEN];        /* norm after FFN       */

/* ---------------- activations ---------------- */

static float x[SEQ][HIDDEN];                   /* layer input          */
#define PIM_OUT_ALIGN __attribute__((aligned(8)))
static float Q[SEQ][HIDDEN] PIM_OUT_ALIGN, K[SEQ][HIDDEN] PIM_OUT_ALIGN,
             V[SEQ][HIDDEN] PIM_OUT_ALIGN;     /* K, V = the cache decode inherits */
static float att[HEADS][SEQ][SEQ];             /* scores, then weights */
static float ctx[SEQ][HIDDEN];                 /* heads concatenated   */
static float proj[SEQ][HIDDEN];                /* attention output     */
static float norm1[SEQ][HIDDEN];
static float hid[SEQ][FFN];                    /* FFN intermediate     */
static float ff[SEQ][HIDDEN];
static float out[SEQ][HIDDEN];

/* results, volatile so they survive -O3 */
volatile float  g_out[4];
volatile double g_checksum;

/* ---------------- matmuls: all OpenBLAS ---------------- */

/* y[t][o] = b[o] + sum_i W[o][i]*in[t][i]  ==  C = in * W^T, C seeded to b.
 * SEQ is a compile-time constant, so the branch folds away. */
static void linear(const wt_t *W, const float *b, const float *in, float *y,
                   int n_out, int n_in) {
    for (int t = 0; t < SEQ; t++)                      /* seed C with bias */
        for (int o = 0; o < n_out; o++) y[(uint64_t)t * n_out + o] = b[o];

#ifdef BLAS_FP16
    /* shgemm takes BOTH operands as FP16, so the activations are narrowed here.
     * That conversion stays INSIDE the timed region deliberately: the PIM path
     * pays the same tax in pim_store_vec, which also narrows the vector to
     * FP16 before the offload. Charging it on one side only would be the exact
     * asymmetry this whole build exists to remove.
     * W2's input is the widest (SEQ x FFN), so the scratch is sized for it. */
    static wt_t in16[(uint64_t)SEQ * FFN];
    for (uint64_t i = 0; i < (uint64_t)SEQ * n_in; i++) in16[i] = (wt_t)in[i];

    if (SEQ == 1)                                      /* GEMV, the decode shape */
        cblas_shgemv(CblasRowMajor, CblasNoTrans, n_out, n_in,
                     1.0f, (wt_t *)W, n_in, in16, 1, 1.0f, y, 1);
    else                                               /* GEMM: C[SEQ][n_out] */
        cblas_shgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                     SEQ, n_out, n_in, 1.0f, in16, n_in, W, n_in, 1.0f, y, n_out);
#else
    if (SEQ == 1)                                      /* GEMV: W[n_out][n_in]*x */
        cblas_sgemv(CblasRowMajor, CblasNoTrans, n_out, n_in,
                    1.0f, W, n_in, in, 1, 1.0f, y, 1);
    else                                               /* GEMM: C[SEQ][n_out] */
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                    SEQ, n_out, n_in, 1.0f, in, n_in, W, n_in, 1.0f, y, n_out);
#endif
}

/* Prefill attention: all SEQ tokens at once, so both halves are GEMM.
 * Head h is the column block at off, leading dimension HIDDEN, so BLAS
 * addresses it directly -- no copy, no transpose. */

/* IDENTICAL to gpt3.c's freestanding math. The baseline used libm expf/tanhf,
 * which measured 1.8x slower per exp and 20x on the GELU block -- a difference
 * in the comparison that has nothing to do with PIM. Same code both sides now. */
#define k_sqrtf(v) __builtin_sqrtf(v)

/* e^v via 2^n * e^r, r = v - n*ln2, |r| <= ln2/2; degree-6 Taylor on r */
static float k_expf(float v) {
    if (v >  87.0f) return INFINITY;
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

static void attention(void) {
    const float scale = 1.0f / k_sqrtf((float)HEAD_DIM);

    for (int h = 0; h < HEADS; h++) {
        const int off = h * HEAD_DIM;

        /* scores: att[h] = scale * Qh * Kh^T. Full SEQ x SEQ, as PyTorch's eager
         * path does; the causal mask is applied in the softmax. */
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans, SEQ, SEQ, HEAD_DIM,
                    scale, &Q[0][off], HIDDEN, &K[0][off], HIDDEN,
                    0.0f, &att[h][0][0], SEQ);

        for (int i = 0; i < SEQ; i++) {        /* causal softmax: token i sees 0..i */
            float *a = att[h][i];
            float m = a[0];
            for (int j = 1; j <= i; j++) if (a[j] > m) m = a[j];
            float sum = 0.0f;
            for (int j = 0; j <= i; j++) { a[j] = k_expf(a[j] - m); sum += a[j]; }
            for (int j = 0; j <= i; j++) a[j] /= sum;
            for (int j = i + 1; j < SEQ; j++) a[j] = 0.0f;   /* future tokens */
        }

        /* context: ctx_h = att[h] * Vh */
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, SEQ, HEAD_DIM, SEQ,
                    1.0f, &att[h][0][0], SEQ, &V[0][off], HIDDEN,
                    0.0f, &ctx[0][off], HIDDEN);
    }
}

/* ---------------- elementwise: no BLAS routine exists ---------------- */

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

/* tanh form -- match with F.gelu(x, approximate='tanh') on the Python side */
static float gelu(float v) {
    const float c = 0.7978845608028654f;   /* sqrt(2/pi) */
    return 0.5f * v * (1.0f + k_tanhf(c * (v + 0.044715f * v * v * v)));
}

static void residual_add(const float *a, const float *b_, float *y, int n) {
    for (int i = 0; i < n; i++) y[i] = a[i] + b_[i];
}

/* ---------------- the layer ---------------- */

/* GPT-3 block: PRE-layernorm, matching gpt3.c line for line. */
static void bert_layer(void) {
    /* attention block. norm1 = LN(x) feeds Q/K/V. */
    layernorm(&x[0][0], g1, beta1, &norm1[0][0]);
    linear(&Wq[0][0], bq, &norm1[0][0], &Q[0][0], HIDDEN, HIDDEN);
    linear(&Wk[0][0], bk, &norm1[0][0], &K[0][0], HIDDEN, HIDDEN);
    linear(&Wv[0][0], bv, &norm1[0][0], &V[0][0], HIDDEN, HIDDEN);
    PHASE();                               /* [1] LN 1 + QKV                 */

    attention();
    PHASE();                               /* [2] attention                  */

    linear(&Wo[0][0], bo, &ctx[0][0], &proj[0][0], HIDDEN, HIDDEN);
    PHASE();                               /* [3] Wo                         */

    /* residual, then the FFN's own pre-norm */
    residual_add(&x[0][0], &proj[0][0], &proj[0][0], SEQ * HIDDEN);
    layernorm(&proj[0][0], g2, beta2, &norm1[0][0]);
    PHASE();                               /* [4] residual 1 + LN 2          */

    /* feed-forward block */
    linear(&W1[0][0], b1, &norm1[0][0], &hid[0][0], FFN, HIDDEN);
    PHASE();                               /* [5] W1                         */

    for (int i = 0; i < SEQ * FFN; i++) (&hid[0][0])[i] = gelu((&hid[0][0])[i]);
    PHASE();                               /* [6] GELU                       */

    linear(&W2[0][0], b2, &hid[0][0], &ff[0][0], HIDDEN, FFN);
    PHASE();                               /* [7] W2                         */

    /* residual only -- pre-LN puts the final norm at the end of the stack. */
    residual_add(&proj[0][0], &ff[0][0], &out[0][0], SEQ * HIDDEN);
    PHASE();                               /* [8] residual 2                 */
}

static void init_params(void) {
    const float sh = 1.0f / k_sqrtf((float)HIDDEN);   /* fan_in = 768  */
    const float sf = 1.0f / k_sqrtf((float)FFN);      /* fan_in = 3072 */

    rng_state = SEED;

    fill_w(&Wq[0][0], (uint64_t)HIDDEN * HIDDEN, sh); fill(bq, HIDDEN, sh);
    fill_w(&Wk[0][0], (uint64_t)HIDDEN * HIDDEN, sh); fill(bk, HIDDEN, sh);
    fill_w(&Wv[0][0], (uint64_t)HIDDEN * HIDDEN, sh); fill(bv, HIDDEN, sh);
    fill_w(&Wo[0][0], (uint64_t)HIDDEN * HIDDEN, sh); fill(bo, HIDDEN, sh);

    fill_w(&W1[0][0], (uint64_t)FFN * HIDDEN, sh);    fill(b1, FFN, sh);
    fill_w(&W2[0][0], (uint64_t)HIDDEN * FFN, sf);    fill(b2, HIDDEN, sf);

    fill(g1, HIDDEN, 0.1f); for (int i = 0; i < HIDDEN; i++) g1[i] += 1.0f;
    fill(beta1, HIDDEN, 0.1f);
    fill(g2, HIDDEN, 0.1f); for (int i = 0; i < HIDDEN; i++) g2[i] += 1.0f;
    fill(beta2, HIDDEN, 0.1f);

    fill(&x[0][0], (uint64_t)SEQ * HIDDEN, 1.0f);     /* layer input */
    /* no history to fill: prefill starts from an empty KV cache */
}

/* OpenBLAS mmaps its packing buffers on the first call of each routine. Spend
 * that once, on 8x8 scratch, before the ROI opens. SEQ-independent. */
static void warmup(void) {
    static float a[64], b[64], c[64];
    for (int i = 0; i < 64; i++) { a[i] = 1.0f; b[i] = 1.0f; }
    cblas_sgemv(CblasRowMajor, CblasNoTrans, 8, 8, 1.0f, a, 8, b, 1, 0.0f, c, 1);
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, 8, 8, 8,
                1.0f, a, 8, b, 8, 0.0f, c, 8);
}

#ifdef CHECK_ATTN
/* plain causal attention in double, from the same Q/K/V; max |ctx - ref| */
static double check_attention(void) {
    static double p[SEQ];
    double worst = 0.0;
    for (int h = 0; h < HEADS; h++) {
        const int off = h * HEAD_DIM;
        for (int i = 0; i < SEQ; i++) {
            double m = -INFINITY, sum = 0.0;
            for (int j = 0; j <= i; j++) {
                double s = 0.0;
                for (int d = 0; d < HEAD_DIM; d++) s += (double)Q[i][off + d] * K[j][off + d];
                p[j] = s / sqrt((double)HEAD_DIM);
                if (p[j] > m) m = p[j];
            }
            for (int j = 0; j <= i; j++) { p[j] = exp(p[j] - m); sum += p[j]; }
            for (int d = 0; d < HEAD_DIM; d++) {
                double r = 0.0;
                for (int j = 0; j <= i; j++) r += p[j] / sum * V[j][off + d];
                double e = fabs(r - ctx[i][off + d]);
                if (e > worst) worst = e;
            }
        }
    }
    return worst;
}
#endif

int main(void) {
    init_params();
    warmup();                               /* allocation kept out of the ROI */

    m5_reset_stats();                       /* ROI start: weight init excluded */
    for (int n = 0; n < ITERS; n++) bert_layer();
    m5_dump_reset_stats();                  /* ROI end                         */

    double sum = 0.0;
    for (int t = 0; t < SEQ; t++)
        for (int i = 0; i < HIDDEN; i++) sum += out[t][i];

    for (int i = 0; i < 4; i++) g_out[i] = out[0][i];
    g_checksum = sum;

    printf("gpt3 prefill: hidden=%d heads=%d ffn=%d seq=%d iters=%d\n",
           HIDDEN, HEADS, FFN, SEQ, ITERS);
    printf("out[0][0..3] = %.6f %.6f %.6f %.6f\n",
           out[0][0], out[0][1], out[0][2], out[0][3]);
    printf("checksum     = %.6f\n", sum);
#ifdef CHECK_ATTN
    double err = check_attention();
    printf("attention check: max |diff| = %.2e -> %s\n", err, err < 1e-4 ? "PASS" : "FAIL");
#endif
    return (int)sum & 0xff;
}
