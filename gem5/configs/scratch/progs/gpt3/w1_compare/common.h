#ifndef W1_COMPARE_COMMON_H
#define W1_COMPARE_COMMON_H

#include <stdint.h>

#ifndef W1_INPUTS
#define W1_INPUTS 2560
#endif
#ifndef W1_OUTPUTS
#define W1_OUTPUTS 10240
#endif
#define W1_MAX_VECTORS 64
#ifndef W1_VECTORS
#define W1_VECTORS 1
#endif
#if W1_INPUTS < 1 || W1_OUTPUTS < 1 || W1_VECTORS < 1 || W1_VECTORS > W1_MAX_VECTORS
#error "Invalid W1 benchmark dimensions or vector count"
#endif

#define W1_SEED 20260816u
static volatile unsigned w1_vectors = W1_VECTORS;

static inline float w1_random(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return (float)(*state >> 8) / 8388608.0f * 2.0f - 1.0f;
}

static inline void w1_reset_stats(void) {
#ifdef __riscv
    register uint64_t a0 asm("a0") = 0, a1 asm("a1") = 0;
    __asm__ volatile(".word 0x8000007B" :: "r"(a0), "r"(a1) : "memory");
#endif
}

static inline void w1_dump_stats(void) {
#ifdef __riscv
    register uint64_t a0 asm("a0") = 0, a1 asm("a1") = 0;
    __asm__ volatile(".word 0x8200007B" :: "r"(a0), "r"(a1) : "memory");
#endif
}

static inline void w1_init_done(void) {
#if defined(__riscv) && defined(W1_FAST_INIT)
    register uint64_t a0 asm("a0") = 0, a1 asm("a1") = 0;
    __asm__ volatile(".word 0xb400007b" :: "r"(a0), "r"(a1) : "memory");
    __asm__ volatile(".rept 64\n\tnop\n\t.endr" ::: "memory");
#endif
}

#endif
