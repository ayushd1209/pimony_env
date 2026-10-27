/* CPU half of the W1 token-vector comparison. */
#include <math.h>
#include <stdio.h>
#include <cblas.h>

#include "common.h"

#ifndef __FLT16_MAX__
#error "The FP16 benchmark requires compiler _Float16 support; include zfh in -march"
#endif
_Static_assert(__builtin_types_compatible_p(hfloat16, _Float16),
               "OpenBLAS hfloat16 must be _Float16");

void cblas_shgemv(enum CBLAS_ORDER order, enum CBLAS_TRANSPOSE trans,
                  blasint m, blasint n, float alpha,
                  hfloat16 *a, blasint lda, hfloat16 *x, blasint incx,
                  float beta, float *y, blasint incy);

static hfloat16 weights[W1_OUTPUTS][W1_INPUTS];
static float bias[W1_OUTPUTS];
static float inputs[W1_MAX_VECTORS][W1_INPUTS];
static hfloat16 input16[W1_MAX_VECTORS][W1_INPUTS];
static float outputs[W1_MAX_VECTORS][W1_OUTPUTS];
static unsigned char scrub[4u << 20] __attribute__((aligned(64)));
volatile double w1_checksum;

static void init_data(void) {
    uint32_t state = W1_SEED;
    const float scale = 1.0f / __builtin_sqrtf((float)W1_INPUTS);
    for (unsigned o = 0; o < W1_OUTPUTS; o++)
        for (unsigned i = 0; i < W1_INPUTS; i++)
            weights[o][i] = (hfloat16)(w1_random(&state) * scale);
    for (unsigned o = 0; o < W1_OUTPUTS; o++) bias[o] = w1_random(&state) * scale;
    for (unsigned t = 0; t < W1_MAX_VECTORS; t++)
        for (unsigned i = 0; i < W1_INPUTS; i++) inputs[t][i] = w1_random(&state);
}

static void warmup(void) {
    static hfloat16 a[64], b[64];
    static float c[64];
    for (unsigned i = 0; i < 64; i++) { a[i] = (hfloat16)1.0f; b[i] = (hfloat16)1.0f; }
    cblas_shgemv(CblasRowMajor, CblasNoTrans, 8, 8,
                 1.0f, a, 8, b, 1, 0.0f, c, 1);
    cblas_shgemm(CblasRowMajor, CblasNoTrans, CblasTrans, 8, 8, 8,
                 1.0f, a, 8, b, 8, 0.0f, c, 8);
}

static void cache_scrub(void) {
    volatile const unsigned char *p = scrub;
    for (unsigned i = 0; i < sizeof(scrub); i += 64) (void)p[i];
}

static void project(unsigned vectors) {
    for (unsigned t = 0; t < vectors; t++)
        for (unsigned i = 0; i < W1_INPUTS; i++) input16[t][i] = (hfloat16)inputs[t][i];
    for (unsigned t = 0; t < vectors; t++)
        for (unsigned o = 0; o < W1_OUTPUTS; o++) outputs[t][o] = bias[o];

    if (vectors == 1)
        cblas_shgemv(CblasRowMajor, CblasNoTrans, W1_OUTPUTS, W1_INPUTS,
                     1.0f, &weights[0][0], W1_INPUTS, &input16[0][0], 1,
                     1.0f, &outputs[0][0], 1);
    else
        cblas_shgemm(CblasRowMajor, CblasNoTrans, CblasTrans,
                     vectors, W1_OUTPUTS, W1_INPUTS,
                     1.0f, &input16[0][0], W1_INPUTS, &weights[0][0], W1_INPUTS,
                     1.0f, &outputs[0][0], W1_OUTPUTS);
}

#ifdef W1_CHECK
static float narrow_reference(unsigned vector, unsigned output) {
    volatile float result = bias[output];
    /* Installed ZVL256B SHGEMM_Q=128; its kernel accumulates each block in FP16. */
    for (unsigned base = 0; base < W1_INPUTS; base += 128) {
        unsigned end = base + 128 < W1_INPUTS ? base + 128 : W1_INPUTS;
        volatile _Float16 acc = (_Float16)0.0f;
        for (unsigned i = base; i < end; i++) {
            const double term = (double)weights[output][i] * (double)input16[vector][i];
            acc = (_Float16)((double)acc + term);
        }
        result = (float)(result + (float)acc);
    }
    return result;
}

static int check_samples(unsigned vectors) {
    const unsigned sample_outputs[] = {0, W1_OUTPUTS / 3, 2 * W1_OUTPUTS / 3, W1_OUTPUTS - 1};
    const unsigned sample_vectors[] = {0, vectors - 1};
    double worst = 0.0;
    int ok = 1;
    for (unsigned t = 0; t < 2; t++) {
        const unsigned v = sample_vectors[t];
        for (unsigned s = 0; s < 4; s++) {
            const unsigned o = sample_outputs[s];
            double reference = bias[o], magnitude = fabs((double)bias[o]);
            for (unsigned i = 0; i < W1_INPUTS; i++) {
                const double term = (double)weights[o][i] * (double)input16[v][i];
                reference += term;
                magnitude += fabs(term);
            }
            const double expected = vectors == 1 ? reference : (double)narrow_reference(v, o);
            const double error = fabs((double)outputs[v][o] - expected);
            const double tolerance = vectors == 1 ? 5e-4 * (1.0 + magnitude)
                                                   : 1e-6 * (1.0 + fabs(expected));
            printf("w1 sample: vector=%u output=%u got=%.9g kernel_ref=%.9g error=%.9g fp64_error=%.9g\n",
                   v, o, (double)outputs[v][o], expected, error,
                   fabs((double)outputs[v][o] - reference));
            if (error > worst) worst = error;
            if (!isfinite(outputs[v][o]) || error > tolerance) ok = 0;
        }
    }
    printf("w1 check: %s max_error=%.9g\n", ok ? "PASS" : "FAIL", worst);
    return ok;
}
#endif

int main(void) {
    const unsigned vectors = w1_vectors;
    if (vectors < 1 || vectors > W1_MAX_VECTORS) {
        fprintf(stderr, "w1: vectors must be between 1 and %u\n", (unsigned)W1_MAX_VECTORS);
        return 1;
    }
    init_data();
    w1_init_done();
    warmup();
    cache_scrub();

    w1_reset_stats();
    project(vectors);
    w1_dump_stats();

    double sum = 0.0;
    for (unsigned t = 0; t < vectors; t++)
        for (unsigned o = 0; o < W1_OUTPUTS; o++) sum += outputs[t][o];
    w1_checksum = sum;
    printf("w1 cpu: inputs=%u outputs=%u vectors=%u\n",
           (unsigned)W1_INPUTS, (unsigned)W1_OUTPUTS, vectors);
    printf("w1 checksum: %.9f\n", sum);
#ifdef W1_CHECK
    if (!check_samples(vectors)) return 1;
#endif
    return 0;
}
