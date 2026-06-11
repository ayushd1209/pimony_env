#include <stdio.h>
#include <stdint.h>

/*
 * pim.wait rd, rs1, imm  — custom-0 opcode (0x0B), funct3=0  (I-type)
 *
 * I-type encoding:
 *  [31:20]imm[11:0] [19:15]rs1 [14:12]funct3 [11:7]rd [6:0]opcode
 *  [ 000000000000 ] [ 01010 ]  [   000   ]  [ 01100 ] [ 0001011 ]
 *     imm=0          rs1=a0(10)  funct3=0     rd=a2(12) opcode=0x0B
 *
 *  = 0x0005060B
 *
 * MVP semantics: rs1 = token, rd = status out.
 *   The decoder body just echoes Rd = Rs1, so the returned status should
 *   equal the token passed in — this only proves decode/execute works.
 *   Real behavior (scoreboard check + stall until completion) wired later.
 *   imm[11:0] reserved for future mode bits (wait-all/token, blocking/poll).
 *
 * Isolated test: no pim.dispatch — we feed a known token and check the echo.
 *
 * Registers pinned so the .word encoding matches:
 *   a0 (x10) = rs1 = token
 *   a2 (x12) = rd  = status output
 */
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

int main(void)
{
    uint64_t token = 0xDEADBEEF;   /* known - easy to spot in output */

    printf("Firing pim.wait: token=0x%lx\n", token);

    uint64_t status = pim_wait(token);

    printf("pim.wait done: status=0x%lx (MVP echoes token, should equal 0x%lx)\n",
           status, token);

    return (status == token) ? 0 : 1;
}
