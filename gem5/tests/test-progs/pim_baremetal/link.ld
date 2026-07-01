OUTPUT_ARCH(riscv)
ENTRY(_start)                 /* ELF entry point -> gem5 resets PC here */

SECTIONS
{
  . = 0x80000000;             /* start placing code at the DRAM base */

  .text : {
    *(.text._start)           /* _start first */
    *(.text*)                 /* then all other code */
  }

  .rodata : { *(.rodata*) }   /* read-only data (constants, strings) */
  .data   : { *(.data*) }     /* initialized globals */
  .bss    : { *(.bss*) *(COMMON) }   /* zero-init globals */

  . = ALIGN(16);
  _stack_top = . + 0x4000;    /* reserve 16 KB for the stack, label its top */
}
