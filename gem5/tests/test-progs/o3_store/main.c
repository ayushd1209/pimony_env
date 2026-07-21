#include <stdint.h>

/* Learning trace: ONE store, on the O3 CPU, watched through the pipeline.
   The single store to 0x83000000 is the instruction we follow through
   Fetch -> ... -> executeStore -> Commit -> writebackStores -> completeStore. */

static inline void m5_exit(void){ register uint64_t a0 asm("a0")=0;
    __asm__ volatile(".word 0x4200007B"::"r"(a0):"memory"); }

int main(void)
{
    *(volatile uint64_t *)0x83000000ULL = 0xDEADBEEFULL;   /* <-- THE store */
    m5_exit();
    for(;;){}
    return 0;
}
