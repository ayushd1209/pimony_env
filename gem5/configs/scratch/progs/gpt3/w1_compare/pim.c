/* W1 projection timing with the existing PIM instruction path. */
#include "common.h"

#if !defined(PIM_LAYOUT) || !defined(PIM_GEMV) || !defined(FREESTANDING) || !defined(BAREMETAL) || !defined(NO_IO)
#error "Build with PIM_LAYOUT, PIM_GEMV, FREESTANDING, BAREMETAL and NO_IO"
#endif
#if defined(PIM_ATT) || defined(PIM_BY_DISPATCH) || defined(PIM_OVERLAP) || defined(NO_PIM_FENCE) || defined(NO_PIM_FENCE_DATA)
#error "Use the ordinary complete GEMV path with all fences enabled"
#endif
#ifndef __FLT16_MAX__
#error "Real FP16 support is required; add zfh to -march"
#endif
#ifndef HIDDEN
#define HIDDEN W1_INPUTS
#endif
#ifndef FFN
#define FFN W1_OUTPUTS
#endif
#if HIDDEN != W1_INPUTS || FFN != W1_OUTPUTS
#error "HIDDEN and FFN must match W1_INPUTS and W1_OUTPUTS"
#endif
#if W1_INPUTS % 16 || W1_OUTPUTS % 4
#error "PIM dimensions must contain whole 16-element steps and four-output groups"
#endif

/* Reuse the implemented layout, fences, slab accumulation and completion path. */
#define main w1_unused_layer_main
#include "../gpt3.c"
#undef main

static float w1_input[W1_MAX_VECTORS][W1_INPUTS] __attribute__((aligned(64)));
static float w1_output[W1_MAX_VECTORS][W1_OUTPUTS] __attribute__((aligned(64)));
static unsigned char w1_scrub[4u << 20] __attribute__((aligned(64)));

volatile uint64_t w1_expected_jobs;
volatile uint64_t w1_completed_jobs;
volatile unsigned w1_measured_vectors;
volatile double w1_pim_checksum;

__attribute__((noreturn))
static void w1_fail(uint64_t code) {
    register uint64_t a0 asm("a0") = 0;
    register uint64_t a1 asm("a1") = code;
    __asm__ volatile(".word 0x4400007B" :: "r"(a0), "r"(a1) : "memory");
    for (;;) {}
}

static void w1_init(void) {
    uint32_t state = W1_SEED;
    const float scale = 1.0f / __builtin_sqrtf((float)W1_INPUTS);
    const uint64_t block = (uint64_t)PIM_WAVES(W1_OUTPUTS) * PIM_S_ROW;
    wt_t *weights = &W1[0][0];

    for (int o = 0; o < W1_OUTPUTS; o++) {
        for (int i = 0; i < W1_INPUTS; i++) {
            const int part = i / PIM_MAX_IN;
            const int local = i - part * PIM_MAX_IN;
            weights[(uint64_t)part * block + pim_idx(o, local)] =
                (wt_t)(w1_random(&state) * scale);
        }
    }
    for (int o = 0; o < W1_OUTPUTS; o++) b1[o] = w1_random(&state) * scale;
    for (int t = 0; t < W1_MAX_VECTORS; t++)
        for (int i = 0; i < W1_INPUTS; i++) w1_input[t][i] = w1_random(&state);
}

static void w1_scrub_cache(void) {
    volatile const unsigned char *p = w1_scrub;
    for (unsigned i = 0; i < sizeof(w1_scrub); i += 64) (void)p[i];
}

int main(void) {
    const unsigned vectors = w1_vectors;
    if (vectors == 0 || vectors > W1_MAX_VECTORS) w1_fail(1);
    setup_paging();
    w1_init();
    w1_init_done();
    handler_calls = 0;
    w1_scrub_cache();

    w1_reset_stats();
    for (unsigned t = 0; t < vectors; t++) {
        pim_store_vec(w1_input[t], W1_INPUTS);
        PIM_FLUSH_VEC(W1_INPUTS);
        pim_linear(D_W1, &W1[0][0], b1, w1_output[t], W1_OUTPUTS, W1_INPUTS);
    }
    w1_dump_stats();

    w1_measured_vectors = vectors;
    w1_expected_jobs = (uint64_t)vectors * PIM_PARTS(W1_INPUTS);
    w1_completed_jobs = handler_calls;
    if (w1_completed_jobs != w1_expected_jobs) w1_fail(2);

    double checksum = 0.0;
    for (unsigned t = 0; t < vectors; t++)
        for (int o = 0; o < W1_OUTPUTS; o++) checksum += w1_output[t][o];
    w1_pim_checksum = checksum;
    m5_exit();
    for (;;) {}
}
