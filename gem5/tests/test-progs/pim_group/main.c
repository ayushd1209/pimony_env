#include <stdint.h>

/* Step 6 — GROUP SCOPING: 3 MACs, ONE completion.
   Two dispatches with the completion bit CLEAR accumulate silently; the third
   sets it and closes the group. Only the closing dispatch returns a token, and
   only it raises an interrupt.

   The measurement is a COUNT, read straight off the trace:
     "PIM dispatch"        x3      <- three MACs really ran
     "PIM token N complete" x1     <- but only one completion
     trap_handler entries   x1     <- and the CPU was interrupted once

   Baseline for comparison is fence_e2e (1 MAC -> 1 interrupt); running three
   plain dispatches instead would give 3 interrupts for the same MAC work. */

#define PIM_DONE       (*(volatile uint64_t *)0x100000000ULL)
#define PIM_TOKEN_ASID ((volatile uint64_t *)0x100000008ULL)

/* One dispatch occupies ONE engine (one bankgroup), and an engine holds one MAC
   at a time -- a second MAC sent to a busy engine is treated as PREEMPTION, not
   accumulation. So a batch must spread across engines, which is what a real GEMV
   does anyway.
   Verified mapping (LPDDR5X ini "rorabacobgch", shift_bits=5):
     addr[6:5]=channel  addr[8:7]=BANKGROUP  addr[14:9]=column  addr[18:]=row
   => +0x80 selects the next engine, channel and row unchanged. */
#define OPERAND        0x81000000ULL   /* ch0, bankgroup 0, row 8256 */
#define OPERAND_BG1    0x81000080ULL   /* ch0, bankgroup 1, row 8256 */
#define OPERAND_BG2    0x81000100ULL   /* ch0, bankgroup 2, row 8256 */

/* --- custom instruction wrappers --- */
/* completion bit = encoding bit 25. Same instruction, two settings:
     0x00B5360B  comp=0  mid-group: accumulate, stay silent, no token
     0x02B5360B  comp=1  close the group: report completion, return token   */
static inline void pim_dispatch_mid(uint64_t addr, uint64_t num_macs){
    register uint64_t a0 asm("a0")=addr; register uint64_t a1 asm("a1")=num_macs;
    register uint64_t a2 asm("a2");
    __asm__ volatile(".word 0x00B5360B":"=r"(a2):"r"(a0),"r"(a1)); (void)a2; }
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
/* stats markers: a0=delay, a1=period, both must be 0 (nonzero period = repeating dump) */
static inline void m5_reset_stats(void){ register uint64_t a0 asm("a0")=0, a1 asm("a1")=0;
    __asm__ volatile(".word 0x8000007B"::"r"(a0),"r"(a1):"memory"); }
static inline void m5_dump_reset_stats(void){ register uint64_t a0 asm("a0")=0, a1 asm("a1")=0;
    __asm__ volatile(".word 0x8400007B"::"r"(a0),"r"(a1):"memory"); }

/* --- completion trap handler (verbatim from fence_e2e) --- */
/* handler_calls proves the interrupt count from the CPU side, independently of
   the DRAM trace: it must read 1 after three MACs, not 3. */
volatile uint64_t handler_calls = 0;

__attribute__((interrupt("supervisor"), aligned(4)))
void trap_handler(void){
    handler_calls++;
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

    m5_reset_stats();                    /* ROI start: paging setup excluded   */
    *op = 0x1234;                        /* dirty the operand line             */
    pim_fence_cl(OPERAND);               /* push it out so PIM reads it fresh  */
    pim_fence_cl(OPERAND_BG1);
    pim_fence_cl(OPERAND_BG2);

    /* One BATCH across three engines -- what a GEMV actually does. Each MAC gets
       its own engine, so all three run concurrently; only the last asks to be
       reported. Three MACs of work, one interrupt. */
    pim_dispatch_mid(OPERAND,     64);   /* engine 0: run, stay silent         */
    pim_dispatch_mid(OPERAND_BG1, 64);   /* engine 1: run, stay silent         */
    uint64_t tok = pim_dispatch(OPERAND_BG2, 64);  /* engine 2: close -> token */

    pim_wait(tok);                       /* one wait, for the whole group      */
    pim_fence_inv(OPERAND);              /* drop the stale copy                */
    volatile uint64_t r = *op; (void)r;  /* re-read the result from DRAM       */
    m5_dump_reset_stats();               /* ROI end: same span as fence_e2e    */

    /* handler_calls must be 1. Kept live so it survives to the final stats. */
    volatile uint64_t calls = handler_calls; (void)calls;

    m5_exit();
    for(;;){}
    return 0;
}
