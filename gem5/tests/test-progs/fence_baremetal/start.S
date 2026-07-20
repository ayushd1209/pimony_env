# Minimal M-mode startup for Test 0 (no paging, no traps — cbo works in M-mode).
.section .text._start
.global _start
_start:
    la   sp, _stack_top       # set stack pointer
    call main                 # run the test (stays in M-mode)
1:  wfi                       # park if main returns
    j    1b
