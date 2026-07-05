#include <stdint.h>

/* PIM register window — MUST match pim_reg_base in the gem5 config */
#define PIM_DONE (*(volatile uint64_t *)0x100000000ULL)
/* tokenAsid array: contiguous right after PIM_DONE. asid[token] at base+0x08+token*8 */
#define PIM_TOKEN_ASID ((volatile uint64_t *)0x100000008ULL)

/* pim.dispatch a2, a0, a1  -> returns a token (rd=a2) */
static inline uint64_t pim_dispatch(uint64_t addr, uint64_t size)
{
    register uint64_t a0 asm("a0") = addr;
    register uint64_t a1 asm("a1") = size;
    register uint64_t a2 asm("a2");
    __asm__ volatile (".word 0x00B5360B" : "=r"(a2) : "r"(a0), "r"(a1));
    return a2;
}

/* pim.wait a2, a0  -> blocks until token's completion is in the scoreboard */
static inline uint64_t pim_wait(uint64_t token)
{
    register uint64_t a0 asm("a0") = token;
    register uint64_t a2 asm("a2");
    __asm__ volatile (".word 0x0005060B" : "=r"(a2) : "r"(a0));
    return a2;
}


/* m5_exit: stop the simulation cleanly (a0 = delay ticks = 0 -> now) */
static inline void m5_exit(void)
{
    register uint64_t a0 asm("a0") = 0;
    __asm__ volatile (".word 0x4200007B" : : "r"(a0) : "memory");
}

#define N_DISPATCH 3
static uint8_t pim_buf[N_DISPATCH][64];   /* one DRAM buffer per dispatch */

/* M-mode trap handler: runs when the PIM completion interrupt fires.
   attribute => GCC saves/restores regs and emits `mret` to return.
   aligned(4): mtvec BASE must be 4-byte aligned (low 2 bits = MODE field),
   else mtvec=&handler corrupts the mode and the trap never reaches us. */
__attribute__((interrupt("supervisor"), aligned(4)))
void trap_handler(void)
{
    uint64_t mask = PIM_DONE;                          /* MMIO read: which tokens done */
    for (int t = 0; t < 64; t++) {                     /* for each completed token...  */
        if (mask & (1ULL << t)) {
            uint64_t asid = PIM_TOKEN_ASID[t];         /* MMIO read: whose token is it */
            uint64_t pack = (asid << 16) | (uint64_t)t;/* pack (asid<<16 | token)      */
            __asm__ volatile ("csrw 0x801, %0" :: "r"(pack)); /* feed done + owner ASID */
        }
    }
    PIM_DONE = mask;                                   /* W1C ack -> line drops          */
}

/* Sv39 paging via gigapages  */

static uint64_t root_table[512] __attribute__((aligned(4096)));  /* one 4KB table */

/* Build one identity gigapage PTE: VA==PA, at the given 1GB-aligned phys addr. */
static uint64_t make_gigapage(uint64_t pa)
{
    return ((pa >> 12) << 10) | 0xCF;
}

/* Switch the current address space: satp with MODE=Sv39, given ASID, same table. */
static void set_asid(uint16_t asid)
{
    uint64_t satp = (8ULL << 60) | ((uint64_t)asid << 44) | ((uint64_t)root_table >> 12);
    __asm__ volatile ("csrw satp, %0" :: "r"(satp));
    __asm__ volatile ("sfence.vma");
}

static void setup_paging(void)
{
    root_table[2] = make_gigapage(0x80000000ULL);    /* code + DRAM  */
    root_table[4] = make_gigapage(0x100000000ULL);   /* PIM MMIO     */
    set_asid(1);                                     /* start as ASID 1 */
}

/* Act as process `asid`, then dispatch. Token is tagged with that ASID. */
static uint64_t dispatch_as(uint16_t asid, uint64_t addr, uint64_t size)
{
    set_asid(asid);
    return pim_dispatch(addr, size);
}

/* Act as process `asid`, then wait. Returns 0 if the token is mine, -1 if foreign. */
static uint64_t wait_as(uint16_t asid, uint64_t tok)
{
    set_asid(asid);
    return pim_wait(tok);
}

/* Model NPROC processes, ASIDs 1..NPROC, one token each. */
#define NPROC 2

/* 4.3c isolation test (scalable): process i owns tok[i] (dispatched under ASID i+1).
   A FOREIGN process must be rejected (pim.wait -> -1); the OWNER must succeed (-> 0). */
int main(void)
{
    setup_paging();

    uint64_t tok[NPROC];
    for (int p = 0; p < NPROC; p++)                          /* each process dispatches */
        tok[p] = dispatch_as(p + 1, (uint64_t)pim_buf[p], 64);

    int pass = 1;
    for (int p = 0; p < NPROC; p++) {
        uint16_t owner   = p + 1;
        uint16_t foreign = (p + 1) % NPROC + 1;              /* some other process */
        if (wait_as(foreign, tok[p]) != (uint64_t)-1) pass = 0;  /* foreign must be rejected */
        if (wait_as(owner,   tok[p]) != 0)            pass = 0;  /* owner must succeed */
    }

    if (pass)
        m5_exit();          /* clean exit == isolation held */
    for (;;) { }            /* FAIL -> hang (distinguishable from a pass) */
    return 0;
}
