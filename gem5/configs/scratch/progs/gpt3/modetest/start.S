.section .text._start
.globl _start
_start:
.option push
.option norelax
    la gp, __global_pointer$
.option pop
    la sp, _stack_top
    call main
    li a0, 0
    .word 0x4200007B        /* m5_exit */
1:  j 1b
