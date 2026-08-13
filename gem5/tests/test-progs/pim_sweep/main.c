#include <stdint.h>

/* num_macs sweep — amortisation experiment.
   Same six-step flow as fence_e2e, repeated with a growing num_macs:
     write -> fence.cl -> dispatch(N) -> wait -> fence.inv -> read
   Each iteration logs "PIM dispatch ... num_macs=N token=T" and
   "PIM token T complete", so MAC latency vs N is read straight off the trace.
   Host-side overhead per iteration is fixed by construction; PIM work is not.
   That gap is the amortisation curve. */

#define PIM_DONE       (*(volatile uint64_t *)0x100000000ULL)
#define PIM_TOKEN_ASID ((volatile uint64_t *)0x100000008ULL)
#define OPERAND        0x81000000ULL

static const uint32_t MACS[] = { 64, 128 };
#define NSTEPS (sizeof(MACS)/sizeof(MACS[0]))

/* --- custom instruction wrappers (identical to fence_e2e) --- */
static inline uint64_t pim_dispatch(uint64_t addr, uint64_t num_macs){
    register uint64_t a0 asm("a0")=addr; register uint64_t a1 asm("a1")=num_macs;
    register uint64_t a2 asm("a2");
    __asm__ volatile(".word 0x02B5360B":"=r"(a2):"r"(a0),"r"(a1)); return a2; }
static inline uint64_t pim_wait(uint64_t token){
    register uint64_t a0 asm("a0")=token; register uint64_t a2 asm("a2");
    __asm__ volatile(".word 0x0005060B":"=r"(a2):"r"(a0)); return a2; }
static inline void pim_fence_cl (uint64_t a){ register uint64_t x asm("a0")=a;
    __asm__ volatile(".word 0x0005100B"::"r"(x):"memory"); }
static inline void pim_fence_inv(uint64_t a){ register uint64_t x asm("a0")=a;
    __asm__ volatile(".word 0x0005200B"::"r"(x):"memory"); }
static inline void m5_exit(void){ register uint64_t a0 asm("a0")=0;
    __asm__ volatile(".word 0x4200007B"::"r"(a0):"memory"); }

/* --- completion trap handler (verbatim from fence_e2e) --- */
__attribute__((interrupt("supervisor"), aligned(4)))
void trap_handler(void){
    uint64_t mask = PIM_DONE;
    for (int t=0;t<64;t++){ if (mask & (1ULL<<t)){
        uint64_t asid = PIM_TOKEN_ASID[t];
        uint64_t pack = (asid<<16)|(uint64_t)t;
        __asm__ volatile("csrw 0x801, %0"::"r"(pack)); } }
    PIM_DONE = mask;                       /* W1C ack */
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

int main(void){
    setup_paging();
    volatile uint64_t *op = (volatile uint64_t *)OPERAND;

    for (unsigned i = 0; i < NSTEPS; i++) {
        *op = 0x1234 + i;                        /* dirty the operand line   */
        pim_fence_cl(OPERAND);                   /* push it out to DRAM      */
        uint64_t tok = pim_dispatch(OPERAND, MACS[i]);
        pim_wait(tok);                           /* wait for this dispatch   */
        pim_fence_inv(OPERAND);                  /* drop the stale copy      */
        volatile uint64_t r = *op; (void)r;      /* re-read from DRAM        */
    }

    m5_exit();
    for(;;){}
    return 0;
}
