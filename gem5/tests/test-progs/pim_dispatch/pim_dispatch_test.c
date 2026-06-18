#include <stdio.h>
#include <stdint.h>

/*
 * pim.dispatch rd, rs1, rs2  — custom-0 opcode (0x0B), funct3=0, funct7=0
 *
 * R-type encoding:
 *  [31:25]funct7 [24:20]rs2 [19:15]rs1 [14:12]funct3 [11:7]rd [6:0]opcode
 *  [ 0000000 ]  [ 01011 ]  [ 01010 ]  [  011  ]  [ 01100 ] [ 0001011 ]
 *    funct7=0   rs2=a1(11) rs1=a0(10)  funct3=3   rd=a2(12) opcode=0x0B
 *
 *  = 0x00B5360B
 *
 * We pin registers explicitly so the .word encoding matches:
 *   a0 (x10) = rs1 = DRAM address
 *   a1 (x11) = rs2 = size in bytes
 *   a2 (x12) = rd  = token output
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

static uint8_t pim_buf[64];   /* lives in .bss — DRAM-backed, linker-assigned address */

int main(void)
{
    uint64_t addr  = (uint64_t)pim_buf;
    uint64_t size  = 64;        /* 64 bytes = 1 cache line            */

    /* Fire three dispatches; tokens should come back 0, 1, 2 */
    for (int i = 0; i < 3; i++) {
        printf("Firing pim.dispatch: addr=0x%lx  size=%lu bytes\n", addr, size);
        uint64_t token = pim_dispatch(addr, size);
        printf("pim.dispatch done: token=0x%lx (expect %d)\n", token, i);
    }
    return 0;
}
