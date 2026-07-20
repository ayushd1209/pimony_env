#include <stdint.h>

/* Step 5 — pim.fence in the REAL PIM flow:
     write operand -> pim.fence.cl -> pim.dispatch -> pim.wait -> pim.fence.inv -> read
   PIM does no arithmetic, so the proof is the TRANSACTION ORDER in the trace:
     WR(operand) before dispatch, and RD(re-read) after wait+invalidate.
   Reuses pim_baremetal's paging + completion-interrupt machinery. */

#define PIM_DONE       (*(volatile uint64_t *)0x100000000ULL)
#define PIM_TOKEN_ASID ((volatile uint64_t *)0x100000008ULL)
#define OPERAND        0x81000000ULL   /* identity-mapped, cacheable, clear of code */

/* --- custom instruction wrappers --- */
static inline uint64_t pim_dispatch(uint64_t addr, uint64_t num_macs){
    register uint64_t a0 asm("a0")=addr; register uint64_t a1 asm("a1")=num_macs;
    register uint64_t a2 asm("a2");
    __asm__ volatile(".word 0x00B5360B":"=r"(a2):"r"(a0),"r"(a1)); return a2; }
static inline uint64_t pim_wait(uint64_t token){
    register uint64_t a0 asm("a0")=token; register uint64_t a2 asm("a2");
    __asm__ volatile(".word 0x0005060B":"=r"(a2):"r"(a0)); return a2; }
static inline void pim_fence_cl (uint64_t a){ register uint64_t x asm("a0")=a;
    __asm__ volatile(".word 0x0005100B"::"r"(x):"memory"); }   /* clean: push OUT   */
static inline void pim_fence_inv(uint64_t a){ register uint64_t x asm("a0")=a;
    __asm__ volatile(".word 0x0005200B"::"r"(x):"memory"); }   /* invalidate: pull IN*/
static inline void m5_exit(void){ register uint64_t a0 asm("a0")=0;
    __asm__ volatile(".word 0x4200007B"::"r"(a0):"memory"); }

/* --- completion trap handler (verbatim from pim_baremetal) --- */
__attribute__((interrupt("supervisor"), aligned(4)))
void trap_handler(void){
    uint64_t mask = PIM_DONE;
    for (int t=0;t<64;t++){ if (mask & (1ULL<<t)){
        uint64_t asid = PIM_TOKEN_ASID[t];
        uint64_t pack = (asid<<16)|(uint64_t)t;
        __asm__ volatile("csrw 0x801, %0"::"r"(pack)); } }
    PIM_DONE = mask;                       /* W1C ack */
}

/* --- Sv39 identity paging (subset of pim_baremetal) --- */
static uint64_t root_table[512] __attribute__((aligned(4096)));
static uint64_t make_gigapage(uint64_t pa){ return ((pa>>12)<<10)|0xCF; }
static void setup_paging(void){
    root_table[2] = make_gigapage(0x80000000ULL);   /* code+data+stack+operands */
    root_table[4] = make_gigapage(0x100000000ULL);  /* PIM MMIO                 */
    uint64_t satp = (8ULL<<60) | (1ULL<<44) | ((uint64_t)root_table>>12); /* Sv39, ASID=1 */
    __asm__ volatile("csrw satp, %0"::"r"(satp));
    __asm__ volatile("sfence.vma");
}

int main(void){
    setup_paging();
    volatile uint64_t *op = (volatile uint64_t *)OPERAND;

    *op = 0x1234;              /* 1. host writes operand -> dirty in L1          */
    pim_fence_cl(OPERAND);     /* 2. CLEAN: push operand OUT to DRAM  [WR]       */
    uint64_t tok = pim_dispatch(OPERAND, 64);  /* 3. PIM reads DRAM operands     */
    pim_wait(tok);             /* 4. wait for completion                         */
    pim_fence_inv(OPERAND);    /* 5. INVALIDATE result region                    */
    volatile uint64_t r = *op; /* 6. re-read result from DRAM         [RD]       */
    (void)r;

    m5_exit();
    for(;;){}
    return 0;
}
