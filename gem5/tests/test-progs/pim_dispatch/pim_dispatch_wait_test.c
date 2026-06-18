#include <stdio.h>
#include <stdint.h>

/*
 * Full async PIM loop: pim.dispatch (fire MAC) -> pim.wait (block until done).
 *
 * pim.dispatch a2, a0, a1   = .word 0x00B5360B   (custom-0, funct3=3)
 *   a0=rs1=DRAM addr, a1=rs2=size, a2=rd=token
 *
 * pim.wait a2, a0           = .word 0x0005060B   (custom-0, funct3=0, I-type)
 *   a0=rs1=token, a2=rd=status. Blocks (quiesce) until PIMony posts the
 *   completion interrupt, which wakes the hart.
 */
static inline uint64_t pim_dispatch(uint64_t addr, uint64_t size_bytes)
{
    register uint64_t r_addr  asm("a0") = addr;
    register uint64_t r_size  asm("a1") = size_bytes;
    register uint64_t r_token asm("a2");

    __asm__ volatile (
        ".word 0x00B5360B\n\t"   /* pim.dispatch a2, a0, a1 */
        : "=r" (r_token)
        : "r"  (r_addr), "r" (r_size)
    );
    return r_token;
}

static inline uint64_t pim_wait(uint64_t token)
{
    register uint64_t r_token  asm("a0") = token;
    register uint64_t r_status asm("a2");

    __asm__ volatile (
        ".word 0x0005060B\n\t"   /* pim.wait a2, a0 */
        : "=r" (r_status)
        : "r"  (r_token)
    );
    return r_status;
}

static uint8_t pim_buf[3 * 64];   /* three 64-byte regions, DRAM-backed */

int main(void)
{
    uint64_t base = (uint64_t)pim_buf;
    uint64_t size = 64;

    /* three dispatches at DIFFERENT addresses (base, base+64, base+128) */
    uint64_t t0 = pim_dispatch(base + 0 * 64, size);
    uint64_t t1 = pim_dispatch(base + 1 * 64, size);
    uint64_t t2 = pim_dispatch(base + 2 * 64, size);
    printf("dispatched tokens %lu %lu %lu at 0x%lx 0x%lx 0x%lx\n",
           t0, t1, t2, base + 0, base + 64, base + 128);

    printf("waiting on token %lu...\n", t0); pim_wait(t0);
    printf("waiting on token %lu...\n", t1); pim_wait(t1);
    printf("waiting on token %lu...\n", t2); pim_wait(t2);
    printf("all three waits returned -- multi-dispatch PIM loop complete\n");

    return 0;
}
