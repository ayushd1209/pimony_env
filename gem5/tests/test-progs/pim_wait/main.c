#include <stdint.h>

/* pim.wait wake-up cases. m5_exit = pass, m5_fail(code) = fail (bits at the checks below).
   Case 1 (stats block 1): wait on long job A; short job B finishes first and wakes the hart.
     pim.wait must re-check A, sleep again, and return only after A completes.
   Case 2 (stats block 2): wait on B, which completed during case 1: return without sleeping.
   Pass also needs system.workload.inst.quiesce = 2 in block 1 and 0 in block 2. */

#define PIM_DONE       (*(volatile uint64_t *)0x100000000ULL)
#define PIM_TOKEN_ASID ((volatile uint64_t *)0x100000008ULL)

#define ENG_A   0x81000000ULL  /* ch0 bg0, row 8256 */
#define ENG_B   0x81000080ULL  /* ch0 bg1: its own engine, so B runs alongside A */
#define ENG_C   0x81000100ULL  /* ch0 bg2: warm-up job */
#define MACS_A  256            /* ~1 us; PIMony issues a MAC only if 4*n clk fits before refresh (2083 clk) */
#define MACS_B  32             /* ~170 ns: lands after pim.wait(A) is asleep, long before A */
#define MACS_C  5
#define NLOG    16

/* --- custom instruction wrappers ("memory": keep the n_irq reads where they are) --- */
static inline uint64_t pim_dispatch(uint64_t addr, uint64_t num_macs){
    register uint64_t a0 asm("a0")=addr; register uint64_t a1 asm("a1")=num_macs;
    register uint64_t a2 asm("a2");
    __asm__ volatile(".word 0x02B5360B":"=r"(a2):"r"(a0),"r"(a1):"memory"); return a2; }
static inline uint64_t pim_wait(uint64_t token){
    register uint64_t a0 asm("a0")=token; register uint64_t a2 asm("a2");
    __asm__ volatile(".word 0x0005060B":"=r"(a2):"r"(a0):"memory"); return a2; }
/* same pim.wait, also returns its own address (compared with sepc) */
static inline uint64_t pim_wait_pc(uint64_t token, uint64_t *pc){
    register uint64_t a0 asm("a0")=token; register uint64_t a2 asm("a2"); uint64_t p;
    __asm__ volatile("1: .word 0x0005060B\n\tlla %1, 1b":"=r"(a2),"=&r"(p):"r"(a0):"memory");
    *pc = p; return a2; }
static inline void m5_exit(void){ register uint64_t a0 asm("a0")=0;
    __asm__ volatile(".word 0x4200007B"::"r"(a0):"memory"); }
static inline void m5_fail(uint64_t code){ register uint64_t a0 asm("a0")=0, a1 asm("a1")=code;
    __asm__ volatile(".word 0x4400007B"::"r"(a0),"r"(a1):"memory"); }
/* stats markers: a0=delay, a1=period, both must be 0 (nonzero period = repeating dump) */
static inline void m5_reset_stats(void){ register uint64_t a0 asm("a0")=0, a1 asm("a1")=0;
    __asm__ volatile(".word 0x8000007B"::"r"(a0),"r"(a1):"memory"); }
static inline void m5_dump_reset_stats(void){ register uint64_t a0 asm("a0")=0, a1 asm("a1")=0;
    __asm__ volatile(".word 0x8400007B"::"r"(a0),"r"(a1):"memory"); }

/* --- completion trap handler, with a log of every entry --- */
volatile uint64_t n_irq;              /* handler entries */
volatile uint64_t log_mask[NLOG];     /* PIM_DONE as entry i read it */
volatile uint64_t log_epc[NLOG];      /* sepc of entry i: where the hart was */

/* fence_e2e's handler, but the scan stops at the highest set bit (its 64-bit walk outlasts any MAC) */
__attribute__((interrupt("supervisor"), aligned(4)))
void trap_handler(void){
    uint64_t mask = PIM_DONE, n = n_irq, epc;
    __asm__ volatile("csrr %0, sepc":"=r"(epc));
    if (n < NLOG) { log_mask[n] = mask; log_epc[n] = epc; }
    n_irq = n + 1;
    for (uint64_t t = 0, m = mask; m; t++, m >>= 1) if (m & 1) {
        uint64_t pack = (PIM_TOKEN_ASID[t] << 16) | t;
        __asm__ volatile("csrw 0x5c0, %0"::"r"(pack)); }
    PIM_DONE = mask;                  /* W1C ack */
}

/* --- Sv39 identity paging (verbatim from fence_e2e) --- */
static uint64_t root_table[512] __attribute__((aligned(4096)));
static uint64_t make_gigapage(uint64_t pa){ return ((pa>>12)<<10)|0xCF; }
static void setup_paging(void){
    root_table[2] = make_gigapage(0x80000000ULL);
    root_table[4] = make_gigapage(0x100000000ULL);
    uint64_t satp = (8ULL<<60) | (1ULL<<44) | ((uint64_t)root_table>>12);
    __asm__ volatile("csrw satp, %0"::"r"(satp));
    __asm__ volatile("sfence.vma");
}

static int first_with(uint64_t bit, uint64_t n){
    for (uint64_t i = 0; i < n && i < NLOG; i++) if (log_mask[i] & bit) return (int)i;
    return -1; }

int main(void){
    setup_paging();

    pim_wait(pim_dispatch(ENG_C, MACS_C)); /* warm-up: handler code, stack, MMIO TLB entry */
    while (PIM_DONE) ;                     /* ack has reached the device: nothing left pending */
    n_irq = 0;

    m5_reset_stats();                      /* block 1 = case 1 */
    uint64_t tA = pim_dispatch(ENG_A, MACS_A);
    uint64_t tB = pim_dispatch(ENG_B, MACS_B);
    uint64_t pcA;
    uint64_t rA = pim_wait_pc(tA, &pcA);
    while (PIM_DONE) ;
    m5_dump_reset_stats();                 /* block 2 = case 2 */
    uint64_t irq_before = n_irq;
    uint64_t rB = pim_wait(tB);
    uint64_t irq_after = n_irq;
    m5_dump_reset_stats();

    if (tA >= 64 || tB >= 64 || tA == tB) { m5_fail(1); for(;;){} }
    uint64_t fail = 0, n = n_irq, bA = 1ULL << tA, bB = 1ULL << tB;
    int j = first_with(bB, n), k = first_with(bA, n);
    if (rA != 0)                     fail |= 1 << 1;  /* A's wait did not return done-and-mine */
    if (j < 0 || (log_mask[j] & bA)) fail |= 1 << 2;  /* B did not complete ahead of A */
    else if (log_epc[j] != pcA)      fail |= 1 << 3;  /* B's wake did not land in pim.wait(A) */
    if (k <= j)                      fail |= 1 << 4;  /* A's completion not seen after B's */
    else if (log_epc[k] != pcA)      fail |= 1 << 5;  /* pim.wait(A) had left before A completed */
    if (rB != 0)                     fail |= 1 << 6;  /* B's wait did not return done-and-mine */
    if (irq_after != irq_before)     fail |= 1 << 7;  /* an interrupt arrived inside pim.wait(B) */

    if (fail) m5_fail(fail); else m5_exit();
    for(;;){}
    return 0;
}
