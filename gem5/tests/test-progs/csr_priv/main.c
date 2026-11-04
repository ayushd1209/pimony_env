/* A U-mode write to the PIM completion CSR must raise an illegal-instruction trap.
 * Bits [9:8] of a CSR number give its lowest privilege (0x5C0 -> S); gem5 checks them in
 * arch/riscv/isa/formats/standard.isa. Exit: m5_exit = pass; m5_fail = the U-mode write was
 * accepted (a forged completion), or a trap that is not the U-mode csrw.
 * -DCSR=0x801 builds the same test for the old number. */
#include <stdint.h>

#ifndef CSR
#define CSR 0x5c0
#endif
#define STR_(x) #x
#define STR(x) STR_(x)

static inline void m5_exit(void) {
    register uint64_t a0 asm("a0") = 0;
    __asm__ volatile(".word 0x4200007B" :: "r"(a0) : "memory");
}
static inline void m5_fail(uint64_t code) {
    register uint64_t a0 asm("a0") = 0;
    register uint64_t a1 asm("a1") = code;
    __asm__ volatile(".word 0x4400007B" :: "r"(a0), "r"(a1) : "memory");
}

extern char u_csrw[];                      /* the U-mode csrw below */

__attribute__((naked)) void user_code(void) {
    __asm__ volatile(
        "li   t0, 0x10003\n"               /* (asid 1 << 16) | token 3: a forged completion */
        ".globl u_csrw\n"
        "u_csrw: csrw " STR(CSR) ", t0\n"
        "li   a0, 0\n"                     /* still running: the write was accepted */
        "li   a1, 1\n"
        ".word 0x4400007B\n"               /* m5_fail(1) */
        "1: j 1b\n");
}

__attribute__((interrupt("machine"), aligned(4))) void trap_m(void) {
    uint64_t cause, epc;
    __asm__ volatile("csrr %0, mcause" : "=r"(cause));
    __asm__ volatile("csrr %0, mepc" : "=r"(epc));
    if (cause == 2 && epc == (uint64_t)u_csrw)
        m5_exit();                         /* illegal instruction at the U-mode csrw: pass */
    m5_fail(2);
}

int main(void) {
    __asm__ volatile("csrw " STR(CSR) ", %0" :: "r"(0x10003ULL));   /* M-mode write: allowed */
    __asm__ volatile("csrc mstatus, %0" :: "r"(3ULL << 11));        /* MPP = U */
    __asm__ volatile("csrw mepc, %0" :: "r"((uint64_t)user_code));
    __asm__ volatile("mret");
    return 0;
}
