#include <stdint.h>

/* pim.gemv end-to-end test  (REV-2 layout, FP16).
 *
 * ONE instruction offloads a whole matrix-vector product; PIMony's sequencer in
 * memory_system.cc expands it into MAC commands over the following cycles.
 *
 * A MAC command carries no bank field, so it broadcasts to all 4 banks of its
 * bankgroup and computes 4 DIFFERENT outputs at once. Commands per instruction
 * is therefore ceil(outputs / 4), NOT outputs x steps as in rev-1. The ladder
 * is built so a failure points at one mechanism rather than "it hung":
 *
 *   A   32 outputs x  8 steps ->   8 cmds, streams 0..7 only  (partial wave)
 *   B  128 outputs x 48 steps ->  32 cmds, ALL 32 streams once (concurrency)
 *   C  768 outputs x 48 steps -> 192 cmds, a real FP16 BERT Wq: 6 waves, so
 *                                streams are REVISITED and the busy gate and
 *                                the 96-entry queue backpressure both matter
 *
 * Rev-1's "does the 64+32 split work" phase is gone: in FP16 a 768-long dot is
 * 48 steps and fits one row, and dot_steps > 64 is now a hard error at entry
 * (long reductions are split into partial matmuls by the caller).
 *
 * handler_calls must read 3 at the end -- exactly one completion per GEMV. More
 * than 3 means the drain counter fires early; fewer means a lost MAC.
 *
 * OPERAND CONTRACT: the base must be aligned to the ROW STRIDE -- 256 KB for
 * the LPDDR5X config, and STRICTER than rev-1's 128 KB rank stride, because row
 * is now the top field a dot touches. Anything finer carries into rank/bank/
 * column partway through and splits dots across units; the sequencer enforces
 * this and exits rather than compute wrong addresses. 0x81000000 is a multiple
 * of 0x40000, so it holds. Nothing is written to the operand first: PIMony's
 * MAC engine performs no arithmetic, so the values are irrelevant -- only the
 * addresses are. */

#define PIM_DONE       (*(volatile uint64_t *)0x100000000ULL)
#define PIM_TOKEN_ASID ((volatile uint64_t *)0x100000008ULL)

/* Overridable so the same source can be built with the weights somewhere else:
   the ONLY difference between two such binaries is the number written into the
   descriptor, so the MAC addresses following it proves they come from there. */
#ifndef WBASE
#define WBASE   0x81000000ULL   /* row-stride aligned; phase C spans 1.5 MB */
#endif
/* Where the input vector would live. Not consumed yet -- D2GWRITE is step 4 --
   but it occupies its descriptor slot so the fetch moves the real 16 bytes.
   Clear of phase C's 1.5 MB, and 32 KB aligned as the vector layout needs. */
#define VBASE   0x81200000ULL

/* Phase B's dot length in COLUMN STEPS (FP16: 16 values each). 48 = a 768-long
   BERT dot. Must be <= 64; 65 is now rejected at entry rather than split. */
#ifndef B_STEPS
#define B_STEPS 48
#endif

/* Phase B's output count. Commands = ceil(outputs/4) and consecutive commands
   go to different streams, so this is really "how many streams run at once":
     4 -> 1 stream    16 -> 4    128 -> all 32, one wave    256 -> 32, twice
   Sweep it to find where concurrency stops helping. Rev-1 measured 32 streams
   costing only 0.6% more than 1 for 32x the work. */
#ifndef B_OUTPUTS
#define B_OUTPUTS 128
#endif

/* --- custom instruction wrappers --- */
/* The two BASES live in memory now: a memory request carries ONE address and
   this offload needs TWO, so the pair moved to a descriptor and rs1 points at
   it (D13). 16 B alignment keeps the device's fetch inside a single 32 B chunk.
   One per phase, in one array, so all three share a DRAM row. */
struct pim_gemv_desc { uint64_t w_base; uint64_t v_base; };
static struct pim_gemv_desc g_desc[3] __attribute__((aligned(16)));

/* pim.fence.cl a0 -> custom-0, funct3=1: push the line at a0 out to DRAM so
   the device can see the descriptor. Required by the contract; NOT measurable
   here, since pim.gemv's own uncacheable access cleans that line anyway. */
static inline void pim_fence_cl(const void *p){
    register uint64_t a0 asm("a0") = (uint64_t)p;
    __asm__ volatile(".word 0x0005100B"::"r"(a0):"memory"); }

/* pim.gemv a2, a0, a1 -> custom-0, funct3=5. Same rd/rs1/rs2 assignment as
   pim.dispatch (0x00B5360B, funct3=3), so only bits[14:12] differ.
     a0 = DESCRIPTOR POINTER       (rs1)
     a1 = dot_steps | outputs<<16  (rs2)
     a2 = completion token         (rd)  */
static inline uint64_t pim_gemv(const struct pim_gemv_desc *d, uint32_t outputs,
                                uint32_t dot_steps){
    register uint64_t a0 asm("a0") = (uint64_t)d;
    register uint64_t a1 asm("a1") = (uint64_t)dot_steps
                                   | ((uint64_t)outputs << 16);
    register uint64_t a2 asm("a2");
    __asm__ volatile(".word 0x00B5560B":"=r"(a2):"r"(a0),"r"(a1):"memory");
    return a2; }
/* Fill descriptor i, push it to DRAM, issue. */
static inline uint64_t gemv_issue(int i, uint64_t w, uint64_t v,
                                  uint32_t outputs, uint32_t dot_steps){
    g_desc[i].w_base = w;
    g_desc[i].v_base = v;
    pim_fence_cl(&g_desc[i]);
    return pim_gemv(&g_desc[i], outputs, dot_steps); }
static inline uint64_t pim_wait(uint64_t token){
    register uint64_t a0 asm("a0")=token; register uint64_t a2 asm("a2");
    __asm__ volatile(".word 0x0005060B":"=r"(a2):"r"(a0)); return a2; }
static inline void m5_exit(void){ register uint64_t a0 asm("a0")=0;
    __asm__ volatile(".word 0x4200007B"::"r"(a0):"memory"); }
static inline void m5_reset_stats(void){ register uint64_t a0 asm("a0")=0, a1 asm("a1")=0;
    __asm__ volatile(".word 0x8000007B"::"r"(a0),"r"(a1):"memory"); }
static inline void m5_dump_reset_stats(void){ register uint64_t a0 asm("a0")=0, a1 asm("a1")=0;
    __asm__ volatile(".word 0x8400007B"::"r"(a0),"r"(a1):"memory"); }

/* --- completion trap handler (verbatim from pim_issue) --- */
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

/* --- Sv39 identity paging (verbatim from pim_issue) --- */
static uint64_t root_table[512] __attribute__((aligned(4096)));
static uint64_t make_gigapage(uint64_t pa){ return ((pa>>12)<<10)|0xCF; }
static void setup_paging(void){
    root_table[2] = make_gigapage(0x80000000ULL);
    root_table[4] = make_gigapage(0x100000000ULL);
    uint64_t satp = (8ULL<<60) | (1ULL<<44) | ((uint64_t)root_table>>12);
    __asm__ volatile("csrw satp, %0"::"r"(satp));
    __asm__ volatile("sfence.vma");
}

/* results, volatile so they survive -O3 and can be read from a memory dump */
volatile uint64_t g_tok[3];

int main(void){
    setup_paging();

#ifndef GEMV_ONLY_B
    /* A: 8 commands on streams 0..7, one wave, nothing revisited. Smallest
       thing that can work -- if this hangs, the instruction or the payload is
       wrong, not the concurrency or the queue. Also checks that a PARTIAL wave
       issues ceil(32/4)=8 commands and does not round up to a whole wave of 32,
       which would MAC past the end of the operand. */
    m5_reset_stats();
    g_tok[0] = gemv_issue(0, WBASE, VBASE, 32, 8);
    pim_wait(g_tok[0]);
    m5_dump_reset_stats();
#endif

    /* B: exactly one full wave -- 32 commands, all 32 streams, each visited
       once. No stream is ever revisited, so the busy gate should never fire.
       If A passes and B hangs, the problem is concurrency, not the address. */
    m5_reset_stats();
    g_tok[1] = gemv_issue(1, WBASE, VBASE, B_OUTPUTS, B_STEPS);
    pim_wait(g_tok[1]);
    m5_dump_reset_stats();

    /* C: a real FP16 BERT Wq. 192 commands over 6 waves, so every stream is
       revisited 6 times -- this is the first phase where the busy gate actually
       fires, and 192 commands exceed the 96-entry queue, so it is also the real
       test of the drip-feed and WillAcceptTransaction backpressure. */
#ifndef GEMV_ONLY_B
    m5_reset_stats();
    g_tok[2] = gemv_issue(2, WBASE, VBASE, 768, 48);
    pim_wait(g_tok[2]);
    m5_dump_reset_stats();
#endif

    volatile uint64_t calls = handler_calls; (void)calls;   /* expect 3 */

    m5_exit();
    for(;;){}
    return 0;
}
