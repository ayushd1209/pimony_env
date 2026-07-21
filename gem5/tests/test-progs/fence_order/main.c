#include <stdint.h>

/* O3 ORDERING DEMO (dispatch-free) — shows pim.fence's barrier on the
   out-of-order core. Two independent cold-miss loads:
     - WITHOUT fence: O3 overlaps their DRAM reads (memory-level parallelism)
     - WITH pim.fence between them (IsReadBarrier): load B must wait for load A
   Build twice — plain and with -DWITH_FENCE — run both on O3, and compare the
   DRAM read timing of 0x82000000 (A) vs 0x82000040 (B):
     overlap  = reorder allowed (no fence)
     serial   = barrier enforced (fence)
   No pim.dispatch here, so it runs on O3 as-is (pim.fence is a plain CBMOp). */

#define LA   0x82000000ULL   /* load A — cold line                    */
#define LB   0x82000040ULL   /* load B — cold, independent line       */
#define FBUF 0x82000080ULL   /* neutral address for the fence's clean */

static inline void pim_fence_cl(uint64_t a){ register uint64_t x asm("a0")=a;
    __asm__ volatile(".word 0x0005100B"::"r"(x):"memory"); }  /* clean + barrier */
static inline void m5_exit(void){ register uint64_t a0 asm("a0")=0;
    __asm__ volatile(".word 0x4200007B"::"r"(a0):"memory"); }

int main(void)
{
    volatile uint64_t *pa=(volatile uint64_t*)LA, *pb=(volatile uint64_t*)LB;

    uint64_t a = *pa;            /* load A: cold miss -> DRAM (slow)         */
#ifdef WITH_FENCE
    pim_fence_cl(FBUF);          /* read barrier: B may not start before A   */
#endif
    uint64_t b = *pb;            /* load B: cold miss -> DRAM                */

    volatile uint64_t sink = a + b; (void)sink;   /* keep both loads live    */
    m5_exit();
    for(;;){}
    return 0;
}
