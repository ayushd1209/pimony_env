OUTPUT_ARCH(riscv)
ENTRY(_start)                 /* ELF entry point -> gem5 resets PC here */

SECTIONS
{
  . = 0x80000000;             /* start placing code at the DRAM base */

  .text : {
    *(.text._start)           /* _start first */
    *(.text*)                 /* then all other code */
  }

  .rodata : { *(.rodata*) }
  .data   : { *(.data*) }
  .bss    : { *(.bss*) *(COMMON) }

  . = ALIGN(16);
  _stack_top = . + 0x4000;    /* 16 KB stack */
}
