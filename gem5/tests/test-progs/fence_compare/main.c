/*
 * Bare-metal cache-maintenance timing comparison for gem5/pim-isa.
 * Build modes:
 *   MODE=0: one pim.fence.cl per cache line
 *   MODE=1: cbo.clean + FENCE for each cache line
 *   MODE=2: cbo.clean for all lines + one FENCE at the end
 *
 * This is a cache-maintenance microbenchmark, not an end-to-end PIM test.
 * Its cache effects and ordering must be checked independently before
 * treating the numbers as a functionally equivalent PIM hand-off.
 */
#include <stdint.h>

#ifndef NLINES
#define NLINES 64
#endif
#ifndef MODE
#define MODE 0
#endif
#if NLINES < 1 || NLINES > 128
#error NLINES must be 1..128 (avoid L1 capacity effects)
#endif
#if MODE < 0 || MODE > 2
#error MODE must be 0..2
#endif

#define DATA_BASE 0x81000000ULL
#define SCRATCH_ADDR 0x82000000ULL
#define LINE_SIZE 64ULL

static inline void pim_fence_clean(uintptr_t address)
{
    register uintptr_t a0 asm("a0") = address;
    asm volatile (".word 0x0005100B" : : "r"(a0) : "memory");
}

static inline void cbo_clean(uintptr_t address)
{
    register uintptr_t a0 asm("a0") = address;
    asm volatile (".word 0x0015200F" : : "r"(a0) : "memory");
}

static inline void full_fence(void)
{
    asm volatile ("fence iorw,iorw" : : : "memory");
}

static inline void measurement_completion_guard(void)
{
    /* A common measurement guard, outside the sequence being compared.
     * Minor explicitly drains its store buffer before a release AMO;
     * O3 waits for older stores before issuing this atomic operation.
     * The scratch line is warmed before ROI and is separate from operands.
     * This does NOT prove that the maintenance sequences alone order PIM. */
    uintptr_t scratch = SCRATCH_ADDR;
    asm volatile ("amoswap.d.rl zero, zero, (%0)" : : "r"(scratch) : "memory");
}

static inline void m5_reset_stats(void)
{
    register uint64_t a0 asm("a0") = 0;
    register uint64_t a1 asm("a1") = 0;
    asm volatile (".word 0x8000007B" : "+r"(a0) : "r"(a1) : "memory");
}

static inline void m5_dump_reset_stats(void)
{
    register uint64_t a0 asm("a0") = 0;
    register uint64_t a1 asm("a1") = 0;
    asm volatile (".word 0x8400007B" : "+r"(a0) : "r"(a1) : "memory");
}

static inline void m5_exit(void)
{
    register uint64_t a0 asm("a0") = 0;
    asm volatile (".word 0x4200007B" : "+r"(a0) : : "memory");
}

int main(void)
{
    /* Touch and dirty the same cache lines in every build. */
#pragma GCC unroll 1
    for (uintptr_t i = 0; i < NLINES; ++i) {
        volatile uint64_t *address =
            (volatile uint64_t *)(DATA_BASE + i * LINE_SIZE);
        *address = 0x12340000ULL + i;
    }
    *(volatile uint64_t *)SCRATCH_ADDR = 0;

    /* Keep the initial stores out of the measured region. */
    full_fence();
    /* Minor may retire FENCE before its store-buffer barrier completes.
     * A following memory operation waits for that barrier on both cores. */
    uint64_t prepared = *(volatile uint64_t *)(DATA_BASE +
                                             (NLINES - 1) * LINE_SIZE);
    asm volatile ("" : : "r"(prepared) : "memory");
    uint64_t scratch_ready = *(volatile uint64_t *)SCRATCH_ADDR;
    asm volatile ("" : : "r"(scratch_ready) : "memory");
    m5_reset_stats();

#pragma GCC unroll 1
    for (uintptr_t i = 0; i < NLINES; ++i) {
        uintptr_t address = DATA_BASE + i * LINE_SIZE;
#if MODE == 0
        pim_fence_clean(address);
#elif MODE == 1
        cbo_clean(address);
        full_fence();
#else
        cbo_clean(address);
#endif
    }
#if MODE == 2
    full_fence();
#endif

    measurement_completion_guard();

    m5_dump_reset_stats();
    m5_exit();
    for (;;) { asm volatile ("wfi"); }
}
