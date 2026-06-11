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

static uint8_t pim_buf[64];   /* .bss — DRAM-backed, linker-assigned address */

int main(void)
{
    uint64_t addr = (uint64_t)pim_buf;
    uint64_t size = 64;

    printf("dispatch: addr=0x%lx size=%lu\n", addr, size);
    uint64_t token = pim_dispatch(addr, size);
    printf("dispatched, token=0x%lx -- now waiting...\n", token);

    uint64_t status = pim_wait(token);
    printf("WOKE UP from pim.wait: status=0x%lx -- PIM completion received!\n",
           status);

    return 0;
}
