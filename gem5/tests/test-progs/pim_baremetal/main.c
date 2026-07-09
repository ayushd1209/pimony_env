#include <stdint.h>

/* PIM register window — MUST match pim_reg_base in the gem5 config */
#define PIM_DONE (*(volatile uint64_t *)0x100000000ULL)
/* tokenAsid array: contiguous right after PIM_DONE. asid[token] at base+0x08+token*8 */
#define PIM_TOKEN_ASID ((volatile uint64_t *)0x100000008ULL)

/* pim.dispatch a2, a0, a1  -> returns a token (rd=a2)
   a1 = num_macs: number of column-step MACs to sweep (NOT a byte size). */
static inline uint64_t pim_dispatch(uint64_t addr, uint64_t num_macs)
{
    register uint64_t a0 asm("a0") = addr;
    register uint64_t a1 asm("a1") = num_macs;
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

/* ---- Test 1: one VA, two address spaces, two physical pages ---------------
   Both processes dispatch the SAME virtual address (TEST_VA). A's page table
   maps it to PA_A; B's maps it to PA_B. Proof is in the DRAMsim3 log: two
   "PIM dispatch addr=" lines with different PAs from the same source VA. */

#define TEST_VA  0x40000000ULL   /* private KV VA; VPN[2] = 1                            */
#define PA_A     0x80000000ULL   /* A's private gigapage (DRAM)                          */
#define PA_B     0xC0000000ULL   /* B's private gigapage (DRAM)                          */
#define ASID_A   3
#define ASID_B   8

/* Shared read-only weights: SAME phys page mapped into both spaces (VPN[2]=5).
   Dispatched at an 0x8M offset so the logged PA (0x88000000) is distinct from
   the private pages, while both spaces resolve it identically -> sharing. */
#define SHARED_VA 0x148000000ULL
#define PA_W      0x80000000ULL   /* shared gigapage base; dispatch PA = base + 0x8M */

/* One 4KB root table per address space. */
static uint64_t root_table_A[512] __attribute__((aligned(4096)));
static uint64_t root_table_B[512] __attribute__((aligned(4096)));

/* Build one gigapage PTE: 1GB leaf at the given 1GB-aligned phys addr. */
static uint64_t make_gigapage(uint64_t pa)
{
    return ((pa >> 12) << 10) | 0xCF;   /* V R W X A D, supervisor */
}

/* Switch address space: satp with MODE=Sv39, given ASID, given root table. */
static void set_asid(uint16_t asid, uint64_t *table)
{
    uint64_t satp = (8ULL << 60) | ((uint64_t)asid << 44) | ((uint64_t)table >> 12);
    __asm__ volatile ("csrw satp, %0" :: "r"(satp));
    __asm__ volatile ("sfence.vma");
}

/* Entries every address space needs to keep the program itself running. */
static void map_common(uint64_t *t)
{
    t[2] = make_gigapage(0x80000000ULL);    /* identity: code + data + stack */
    t[4] = make_gigapage(0x100000000ULL);   /* identity: PIM MMIO            */
}

static void setup_paging(void)
{
    map_common(root_table_A);
    map_common(root_table_B);
    /* the crux: SAME VA -> DIFFERENT phys page in each address space */
    root_table_A[TEST_VA >> 30] = make_gigapage(PA_A) & ~0x04ULL;
    root_table_B[TEST_VA >> 30] = make_gigapage(PA_B) & ~0x04ULL;
    /* shared read-only weights: SAME PPN in both address spaces */
    root_table_A[SHARED_VA >> 30] = make_gigapage(PA_W) & ~0x04ULL;
    root_table_B[SHARED_VA >> 30] = make_gigapage(PA_W) & ~0x04ULL;
    set_asid(ASID_A, root_table_A);         /* start in A's space */
}

/* Act as (asid, table), then dispatch TEST_VA. */
static uint64_t dispatch_as(uint16_t asid, uint64_t *table, uint64_t va, uint64_t num_macs)
{
    set_asid(asid, table);
    return pim_dispatch(va, num_macs);
}

/* Act as (asid, table), then wait for the token. */
static uint64_t wait_as(uint16_t asid, uint64_t *table, uint64_t tok)
{
    set_asid(asid, table);
    return pim_wait(tok);
}

/* Busy spin to space the two dispatches apart in time (NOT a completion wait).
   Goal: let A's MAC drain before B fires, so they don't overlap in flight and
   the DPSA cross-subarray preemption never triggers. Tune the count if needed. */
static void delay(volatile uint64_t n)
{
    while (n--) __asm__ volatile ("nop");
}

int main(void)
{
    setup_paging();

    /* SHARING: both spaces dispatch SHARED_VA -> SAME PA (0x88000000). */
    uint64_t tokSA = dispatch_as(ASID_A, root_table_A, SHARED_VA, 64);
    delay(2000);
    uint64_t tokSB = dispatch_as(ASID_B, root_table_B, SHARED_VA, 64);
    wait_as(ASID_A, root_table_A, tokSA);
    wait_as(ASID_B, root_table_B, tokSB);

    /* ISOLATION: both spaces dispatch TEST_VA -> DIFFERENT PA (PA_A, PA_B). */
    uint64_t tokA = dispatch_as(ASID_A, root_table_A, TEST_VA, 64);
    delay(2000);
    uint64_t tokB = dispatch_as(ASID_B, root_table_B, TEST_VA, 64);
    wait_as(ASID_A, root_table_A, tokA);
    wait_as(ASID_B, root_table_B, tokB);

    m5_exit();
    for (;;) { }
    return 0;
}
