.section .text._start       # the section the linker places first
.global _start
_start:
    la   sp, _stack_top       #  set the stack pointer (from the linker symbol)

    la   t0, trap_m           #  M-mode handler: the U-mode csrw must trap here
    csrw mtvec, t0

    li   t0, -1               #  PMP entry 0: NAPOT region covering all memory
    csrw pmpaddr0, t0
    li   t0, 0x1f             #  cfg = A=NAPOT(0x18) | X(4) | W(2) | R(1)
    csrw pmpcfg0, t0          #  so U-mode can fetch and run

    call main                 #  main writes the CSR in M-mode, then mrets to U-mode
1:  j    1b
