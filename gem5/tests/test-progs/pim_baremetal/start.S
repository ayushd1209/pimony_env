.section .text._start       # the section the linker places first
.global _start
_start:
    la   sp, _stack_top       #  set the stack pointer (from the linker symbol)

    li t0, (1<<24)
    csrs mideleg, t0

    la   t0, trap_handler     #    tell the CPU where the handler is
    csrw stvec, t0            #    stvec

    li   t0, (1 << 24)        #    per-source enable: bit 24
    csrs sie, t0              #    sie[24] = 1  (enable the PIM interrupt line)
    csrsi sstatus, 0x2        #    sstatus.SIE = bit 1

    li   t0, -1               #    PMP entry 0: NAPOT region covering all memory
    csrw pmpaddr0, t0
    li   t0, 0x1f             #   cfg = A=NAPOT(0x18) | X(4) | W(2) | R(1)
    csrw pmpcfg0, t0          #   grant S-mode R/W/X to all memory

    li   t0, (3 << 11)        #    MPP field = mstatus[12:11]
    csrc mstatus, t0          #    clear MPP
    li   t0, (1 << 11)        #    MPP = 0b01 (S-mode)
    csrs mstatus, t0          #    set MPP = S
    la   t0, main             #    mret target
    csrw mepc, t0             #    mepc = main

    mret                      #  return to S-mode, enter main
1:  wfi                       # if main returns, just park here
    j    1b
