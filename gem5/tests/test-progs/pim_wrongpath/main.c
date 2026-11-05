#include <stdint.h>

/* WRONG-PATH LAUNCH: a launch fetched past a mispredicted branch must not reach
   the PIM, must give its token back, and must leave rd unchanged.
   Each round draws a pseudo-random bit: 1 = launch and wait, 0 = skip. The
   predictor cannot learn the bits, so it guesses "launch" on about half of the
   skips. The branch waits on a load that misses to DRAM, so O3 executes that
   wrong-path launch (and takes a token) before the branch resolves.
   Proof the wrong path ran on O3: --debug-flags=LSQUnit prints
   "PIM token N returned by squashed" for each launch squashed after taking a token.
   A leak shows up as an empty pool: a later launch gets rd >= 64.
   Exit: m5_exit = pass. m5_fail(1) = a launch got no valid token (pool leaked dry),
   (2) = pim.wait did not return 0, (3) = rd changed on the skip path. */

#define PIM_DONE       (*(volatile uint64_t *)0x100000000ULL)
#define PIM_TOKEN_ASID ((volatile uint64_t *)0x100000008ULL)
#define OPERAND        0x81000000ULL   /* ch0, bankgroup 0, as pim_issue */
#ifndef ITERS
#define ITERS          1024            /* ~250 wrong-path launches expected: > 64 tokens */
#endif
#define NMACS          5

static inline void m5_exit(void){ register uint64_t a0 asm("a0")=0;
    __asm__ volatile(".word 0x4200007B"::"r"(a0):"memory"); }
static inline void m5_fail(uint64_t code){ register uint64_t a0 asm("a0")=0;
    register uint64_t a1 asm("a1")=code;
    __asm__ volatile(".word 0x4400007B"::"r"(a0),"r"(a1):"memory"); }

/* --- completion trap handler (verbatim from fence_e2e) --- */
__attribute__((interrupt("supervisor"), aligned(4)))
void trap_handler(void){
    uint64_t mask = PIM_DONE;
    for (int t=0;t<64;t++){ if (mask & (1ULL<<t)){
        uint64_t asid = PIM_TOKEN_ASID[t];
        uint64_t pack = (asid<<16)|(uint64_t)t;
        __asm__ volatile("csrw 0x5c0, %0"::"r"(pack)); } }
    PIM_DONE = mask;                       /* W1C ack */
}

/* --- Sv39 identity paging. Table preloaded in the image: Minor's walker can read
   it before run-time PTE stores leave the store buffer (GAPS G12) --- */
#define GIGAPAGE(pa) ((((pa)>>12)<<10)|0xCF)
static uint64_t root_table[512] __attribute__((aligned(4096))) = {
    [2] = GIGAPAGE(0x80000000ULL),         /* code + data + stack + operands */
    [4] = GIGAPAGE(0x100000000ULL),        /* PIM MMIO */
};
static void setup_paging(void){
    uint64_t satp = (8ULL<<60) | (1ULL<<44) | ((uint64_t)root_table>>12);
    __asm__ volatile("csrw satp, %0"::"r"(satp));
    __asm__ volatile("sfence.vma");
}

static uint64_t slow[8] __attribute__((aligned(64)));   /* one line, flushed every round */

int main(void){
    setup_paging();
    uint64_t n = ITERS;
    register uint64_t res asm("a0");
    __asm__ volatile(
        "  li    a0, 0\n"
        "  li    s0, 0x9e3779b97f4a7c15\n"          /* xorshift64 state */
        "  li    s5, 0x5a5a\n"                      /* sentinel held in rd */
        "  li    s6, 64\n"                          /* token pool size */
        "1:\n"
        "  slli  t0, s0, 13\n"
        "  xor   s0, s0, t0\n"
        "  srli  t0, s0, 7\n"
        "  xor   s0, s0, t0\n"
        "  slli  t0, s0, 17\n"
        "  xor   s0, s0, t0\n"
        "  srli  t1, s0, 32\n"
        "  andi  t1, t1, 1\n"                       /* 1 = launch, 0 = skip */
        "  .insn i 0x0b, 4, x0, %[slow], 0\n"       /* pim.fence.flush: the next load misses */
        "  ld    t2, 0(%[slow])\n"                  /* from DRAM, always 0 */
        "  xor   t1, t1, t2\n"                      /* so the branch waits for DRAM */
        "  mv    a2, s5\n"
        "  beqz  t1, 2f\n"
        "  .insn r 0x0b, 3, 1, a2, %[op], %[nm]\n"  /* pim.dispatch a2 (completion bit set) */
        "  bgeu  a2, s6, 7f\n"
        "  .insn i 0x0b, 0, a5, a2, 0\n"            /* pim.wait a5, a2 */
        "  bnez  a5, 8f\n"
        "  j     3f\n"
        "2:\n"
        "  bne   a2, s5, 9f\n"                      /* skip path: rd untouched */
        "3:\n"
        "  addi  %[n], %[n], -1\n"
        "  bnez  %[n], 1b\n"
        "  .insn r 0x0b, 3, 1, a2, %[op], %[nm]\n"  /* last launch: the pool still has a token */
        "  bgeu  a2, s6, 7f\n"
        "  .insn i 0x0b, 0, a5, a2, 0\n"
        "  bnez  a5, 8f\n"
        "  j     4f\n"
        "7:\n"
        "  li    a0, 1\n"
        "  j     4f\n"
        "8:\n"
        "  li    a0, 2\n"
        "  j     4f\n"
        "9:\n"
        "  li    a0, 3\n"
        "4:\n"
        : "=&r"(res), [n]"+&r"(n)
        : [slow]"r"(slow), [op]"r"(OPERAND), [nm]"r"((uint64_t)NMACS)
        : "t0", "t1", "t2", "a2", "a5", "s0", "s5", "s6", "memory");
    if (res) m5_fail(res);
    m5_exit();
    for(;;){}
    return 0;
}
