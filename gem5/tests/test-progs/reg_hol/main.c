#include <stdint.h>
/* Positive control for the PIMREG "hol" counter (pimony.cc): can a delayed register
   response hold back DRAM responses queued behind it? The CPU streams loads through
   DRAM while a pim.dispatch job runs, so the completion interrupt lands with cache
   misses in flight. The handler's PIM_DONE read then waits pim_reg_latency in the
   device's response queue, and DRAM read responses arriving meanwhile queue behind it.
   Phase 1 (stats dump 1): NJOB rounds of dispatch, stream STREAM_BYTES / NJOB, wait.
   Phase 2 (dump 2): the same stream with a PIM_DONE read every 64 lines and no handler,
   so younger loads miss to DRAM while the register read waits. */
#define PIM_DONE       (*(volatile uint64_t *)0x100000000ULL)
#define PIM_TOKEN_ASID ((volatile uint64_t *)0x100000008ULL)
#define OPERAND        0x81000000ULL   /* ch0, bankgroup 0, as in pim_issue */
#define STREAM         0x90000000ULL   /* loads only, far from the operand row */
#define STREAM_BYTES   (8u << 20)
#define NJOB           8
#define NMACS          64

static inline uint64_t pim_dispatch(uint64_t addr, uint64_t num_macs){
    register uint64_t a0 asm("a0")=addr; register uint64_t a1 asm("a1")=num_macs;
    register uint64_t a2 asm("a2");
    __asm__ volatile(".word 0x02B5360B":"=r"(a2):"r"(a0),"r"(a1)); return a2; }
static inline uint64_t pim_wait(uint64_t token){
    register uint64_t a0 asm("a0")=token; register uint64_t a2 asm("a2");
    __asm__ volatile(".word 0x0005060B":"=r"(a2):"r"(a0)); return a2; }
static inline void pim_fence_cl(uint64_t a){ register uint64_t x asm("a0")=a;
    __asm__ volatile(".word 0x0005100B"::"r"(x):"memory"); }
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
        __asm__ volatile("csrw 0x5c0, %0"::"r"(pack)); } }
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

volatile uint64_t sink;

int main(void){
    setup_paging();
    *(volatile uint64_t *)OPERAND = 0x1234;
    pim_fence_cl(OPERAND);

    const volatile uint64_t *s = (const volatile uint64_t *)STREAM;
    const uint32_t lines = STREAM_BYTES / 64 / NJOB;
    uint64_t sum = 0;
    m5_reset_stats();
    for (int j = 0; j < NJOB; j++) {
        uint64_t tok = pim_dispatch(OPERAND, NMACS);
        for (uint32_t i = 0; i < lines; i++)          /* one load per line while the MAC runs */
            sum += s[((uint64_t)j * lines + i) * 8];
        pim_wait(tok);
    }
    m5_dump_reset_stats();

    for (uint32_t i = 0; i < STREAM_BYTES / 64; i++) {
        sum += s[(uint64_t)i * 8];
        if ((i & 63) == 0)
            sum += PIM_DONE;                          /* register read among DRAM misses */
    }
    m5_dump_reset_stats();
    sink = sum;

    m5_exit();
    for(;;){}
    return 0;
}
