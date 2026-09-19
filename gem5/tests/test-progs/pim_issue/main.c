#include <stdint.h>

/* Issue-rate microbenchmark: how fast can the HOST push out pim.dispatch?
   NDISP dispatches back-to-back, NO pim_wait between them, so nothing on the
   CPU side forces ordering. Read the gaps between consecutive "PIM dispatch"
   lines in the DRAMsim3 trace -- that interval IS the host issue cost.

   Cycles over 4 engines (bankgroups 0-3 of channel 0) so a busy engine never
   back-pressures: with num_macs=5 a MAC is ~19-69 cyc, and each engine is
   revisited only every 4 dispatches.

   num_macs=5 is the REALISTIC value (d_head 80 / 16 FP16 per step), not 64.
   comp=0 on all but the last so we get one interrupt, not NDISP of them. */

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

/* NENG engines, none revisited. e -> channel e%4 (stride 0x20), bankgroup e/4
   (stride 0x80), rank 0 throughout. 16 = 4 ch x 4 bg, which is what one
   pim.gemv covers with num_outputs=64 -- so the two issue paths drive the SAME
   engines and a measured gap is issue cost, not DRAM behaviour. */
/* channel is only 32 B apart, so two engines otherwise share a 64 B cache line
   and priming both trips MSHR::promoteWritable. The +e*0x40000 row offset gives
   each engine its own DRAM row, hence its own line; row bits do not select the
   engine, so the mapping is unchanged. */
#define NENG           32
#define ENG_ADDR(e)    (OPERAND + ((uint64_t)((e) % 4))       * 0x20 \
                                + ((uint64_t)(((e) / 4) % 4)) * 0x80 \
                                + ((uint64_t)((e) / 16))      * 0x20000 \
                                + ((uint64_t)(e))             * 0x40000)

#ifndef NDISP
#define NDISP   16                     /* dispatches issued back-to-back */
#endif
#define NMACS    5                     /* realistic: d_head 80 / 16 per step */

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
    uint64_t ENG[NENG];
    for (int e = 0; e < NENG; e++) ENG[e] = ENG_ADDR(e);
    volatile uint64_t *op = (volatile uint64_t *)OPERAND;

    for (int e = 0; e < NENG; e++) {     /* prime every operand line out to DRAM */
        *(volatile uint64_t *)ENG[e] = 0x1234 + e;
        pim_fence_cl(ENG[e]);
    }

    m5_reset_stats();                   /* ROI = the issue burst only */
    for (int i = 0; i < NDISP - 1; i++) /* NDISP-1 silent dispatches, no waits */
        pim_dispatch_mid(ENG[i % NENG], NMACS);
    uint64_t tok = pim_dispatch(ENG[(NDISP - 1) % NENG], NMACS);  /* last closes group */
    pim_wait(tok);                      /* single wait, after the whole burst */
    m5_dump_reset_stats();

    pim_fence_inv(OPERAND);
    volatile uint64_t r = *op; (void)r;
    volatile uint64_t calls = handler_calls; (void)calls;

    m5_exit();
    for(;;){}
    return 0;
}
