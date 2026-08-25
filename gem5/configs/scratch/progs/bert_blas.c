/* BERT-base layer, host baseline. Every matmul goes through OpenBLAS; the
 * elementwise ops (softmax, GELU, LayerNorm, residuals) stay hand-written
 * because BLAS has no routine for them -- the same split PyTorch uses.
 *
 * Hosted build only: needs libc, libm and OpenBLAS. The bare-metal version
 * used for the FS/PIM runs is bert.c -- keep the two numerically in step.
 *
 * Weights, PRNG and layer order are identical to bert.c, so checksums are
 * comparable to 4-5 significant figures (BLAS sums in a different order).
 *
 * Build:
 *   riscv64-unknown-linux-gnu-gcc -O3 -march=rv64gcv -mabi=lp64d -static \
 *     -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math \
 *     -I/opt/openblas-zvl256b/include -o bert_blas bert_blas.c \
 *     /opt/openblas-zvl256b/lib/libopenblas.a -lm
 *
 * Run:
 *   build/RISCV/gem5.opt --outdir=m5out/X configs/pimony/se_bert.py \
 *       o3 configs/scratch/progs/bert_blas
 */

#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <cblas.h>

/* ---------------- model configuration ---------------- */

#define HIDDEN   768              /* BERT-base d_model                        */
#define HEADS    12
#define HEAD_DIM (HIDDEN / HEADS) /* 64                                       */
#define FFN      3072             /* intermediate_size                        */
#ifndef SEQ
#define SEQ      1                /* tokens per pass. 1 = GEMV (decode-shaped,
                                   * no weight reuse). >1 = GEMM, which is how
                                   * BERT is actually run. Set with -DSEQ=n */
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

/* results, volatile so they survive -O3 */
volatile float  g_out[4];
volatile double g_checksum;

/* ---------------- matmuls: all OpenBLAS ---------------- */

/* y[t][o] = b[o] + sum_i W[o][i]*in[t][i]  ==  C = in * W^T, C seeded to b.
 * SEQ is a compile-time constant, so the branch folds away. */
static void linear(const float *W, const float *b, const float *in, float *y,
                   int n_out, int n_in) {
    for (int t = 0; t < SEQ; t++)                      /* seed C with bias */
        for (int o = 0; o < n_out; o++) y[(uint64_t)t * n_out + o] = b[o];

    if (SEQ == 1)                                      /* GEMV: W[n_out][n_in]*x */
        cblas_sgemv(CblasRowMajor, CblasNoTrans, n_out, n_in,
                    1.0f, W, n_in, in, 1, 1.0f, y, 1);
    else                                               /* GEMM: C[SEQ][n_out] */
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                    SEQ, n_out, n_in, 1.0f, in, n_in, W, n_in, 1.0f, y, n_out);
}

/* Multi-head self-attention over Q/K/V, heads concatenated into ctx.
 * Head h is a COLUMN SLICE of Q/K/V, so &Q[0][off] with a leading dimension of
 * HIDDEN addresses it directly -- no copy, no transpose. att[h] is a
 * contiguous [SEQ][SEQ] block, leading dimension SEQ. */
static void attention(void) {
    const float scale = 1.0f / __builtin_sqrtf((float)HEAD_DIM);

    for (int h = 0; h < HEADS; h++) {
        const int off = h * HEAD_DIM;

        /* scores: att[h] = scale * Qh * Kh^T   (alpha carries the scaling) */
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                    SEQ, SEQ, HEAD_DIM,
                    scale, &Q[0][off], HIDDEN, &K[0][off], HIDDEN,
                    0.0f, &att[h][0][0], SEQ);

        for (int i = 0; i < SEQ; i++) {
            for (int j = 0; j < SEQ; j++)          /* mask, applied after the GEMM */
                if (!mask[j]) att[h][i][j] = -INFINITY;

            float m = att[h][i][0];                /* softmax, max-subtracted */
            for (int j = 1; j < SEQ; j++) if (att[h][i][j] > m) m = att[h][i][j];
            float sum = 0.0f;
            for (int j = 0; j < SEQ; j++) {
                att[h][i][j] = expf(att[h][i][j] - m);
                sum += att[h][i][j];
            }
            for (int j = 0; j < SEQ; j++) att[h][i][j] /= sum;
        }

        /* context: ctx_h = att[h] * Vh */
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                    SEQ, HEAD_DIM, SEQ,
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
        float inv = 1.0f / __builtin_sqrtf(var + LN_EPS);
        for (int i = 0; i < HIDDEN; i++)
            y[(uint64_t)t * HIDDEN + i] = (v[i] - mean) * inv * gain[i] + bias[i];
    }
}

/* tanh form -- match with F.gelu(x, approximate='tanh') on the Python side */
static float gelu(float v) {
    const float c = 0.7978845608028654f;   /* sqrt(2/pi) */
    return 0.5f * v * (1.0f + tanhf(c * (v + 0.044715f * v * v * v)));
}

static void residual_add(const float *a, const float *b_, float *y, int n) {
    for (int i = 0; i < n; i++) y[i] = a[i] + b_[i];
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
    const float sh = 1.0f / __builtin_sqrtf((float)HIDDEN);   /* fan_in = 768  */
    const float sf = 1.0f / __builtin_sqrtf((float)FFN);      /* fan_in = 3072 */

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

/* OpenBLAS mmaps its packing buffers on the first call of each routine. Spend
 * that once, on 8x8 scratch, before the ROI opens. SEQ-independent. */
static void warmup(void) {
    static float a[64], b[64], c[64];
    for (int i = 0; i < 64; i++) { a[i] = 1.0f; b[i] = 1.0f; }
    cblas_sgemv(CblasRowMajor, CblasNoTrans, 8, 8, 1.0f, a, 8, b, 1, 0.0f, c, 1);
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, 8, 8, 8,
                1.0f, a, 8, b, 8, 0.0f, c, 8);
}

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

    printf("bert layer: hidden=%d heads=%d ffn=%d seq=%d iters=%d\n",
           HIDDEN, HEADS, FFN, SEQ, ITERS);
    printf("out[0][0..3] = %.6f %.6f %.6f %.6f\n",
           out[0][0], out[0][1], out[0][2], out[0][3]);
    printf("checksum     = %.6f\n", sum);
    return (int)sum & 0xff;
}
