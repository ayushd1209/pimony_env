#include <stdint.h>

/* PIM register window — MUST match pim_reg_base in the gem5 config */
#define PIM_DONE (*(volatile uint64_t *)0x100000000ULL)

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
__attribute__((interrupt("machine"), aligned(4)))
void trap_handler(void)
{
    uint64_t mask = PIM_DONE;                          /* MMIO read: which tokens done */
    __asm__ volatile ("csrw 0x800, %0" :: "r"(mask));  /* feed scoreboard via pimdone CSR */
    PIM_DONE = mask;                                   /* W1C ack -> line drops          */
}


int main(void)
{
    uint64_t tok[N_DISPATCH];
    for (int i = 0; i < N_DISPATCH; i++)                    /* fire N MACs back-to-back */
        tok[i] = pim_dispatch((uint64_t)pim_buf[i], 64);
    for (int i = 0; i < N_DISPATCH; i++)
        pim_wait(tok[i]);

    m5_exit();
    return 0;
}
