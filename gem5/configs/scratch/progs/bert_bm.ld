/* Bare-metal layout for bert.c on the fence_e2e machine.
 * Same idea as tests/test-progs/pim_baremetal/link.ld, plus the small-data
 * sections and __global_pointer$, which the default linker script would
 * normally provide and a custom script does not. */

OUTPUT_ARCH(riscv)
ENTRY(_start)                    /* gem5 resets the PC to this */

SECTIONS
{
  . = 0x80000000;                /* DRAM base on this machine */

  .text : {
    *(.text._start)              /* _start first */
    *(.text*)
  }

  .rodata : { *(.rodata*) *(.srodata*) }
  .data   : { *(.data*) }

  /* gp addresses small globals with a signed 12-bit offset, so it must sit
     within 2 KB of them. Point it 0x800 past the start of the small data. */
  .sdata : {
    __global_pointer$ = . + 0x800;
    *(.sdata*)
  }

  .sbss : { *(.sbss*) *(.scommon) }
  .bss  : { *(.bss*) *(COMMON) }   /* the 28 MB of weights lands here */

  . = ALIGN(16);
  _stack_top = . + 0x4000;       /* 16 KB stack, label its top */
}
