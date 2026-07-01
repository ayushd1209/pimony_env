.section .text._start       # the section the linker places first
.global _start
_start:
    la   sp, _stack_top     # 1. set the stack pointer (from the linker symbol)

    la   t0, trap_handler     #    tell the CPU where the handler is
    csrw mtvec, t0            #    mtvec = handler addr (4-aligned -> Direct mode)

    li   t0, (1 << 24)        #    per-source enable: bit 24
    csrs mie, t0              #    mie[24] = 1  (enable the PIM interrupt line)

    csrsi mstatus, 0x8        #    global enable: mstatus.MIE (bit 3) = 1

    call main               # 2. enter C
1:  wfi                     # 3. if main returns, just park here
    j    1b
