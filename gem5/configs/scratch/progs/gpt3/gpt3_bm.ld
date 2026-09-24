/* Bare-metal layout for bert.c on the fence_e2e machine.
 * Same idea as tests/test-progs/pim_baremetal/link.ld, plus the small-data
 * sections and __global_pointer$, which the default linker script would
 * normally provide and a custom script does not. */

OUTPUT_ARCH(riscv)
ENTRY(_start)                    /* gem5 resets the PC to this */

/* ONE PT_LOAD for everything. Under -DPIM_LAYOUT the weight arrays are 256 KB
 * aligned, and left to itself ld gives .bss its own segment starting at file
 * offset 0x40000 -- past the end of an 18 KB file. That segment holds no file
 * bytes, but gem5 bounds-checks p_offset before it notices p_filesz is 0
 * (elf_object.cc handleLoadableSegment -> memory_image.hh) and panics with
 * "Segment outside the bounds of the image data". Forcing one segment keeps
 * p_offset at 0x1000; the file bytes stop after .sdata and gem5 zero-fills the
 * rest, which is what .bss means. */
PHDRS
{
  all PT_LOAD FLAGS(7);          /* RWX; bare metal, nothing enforces it */
}

SECTIONS
{
  . = 0x80000000;                /* DRAM base on this machine */

  .text : {
    *(.text._start)              /* _start first */
    *(.text*)
  } :all

  .rodata : { *(.rodata*) *(.srodata*) } :all
  .data   : { *(.data*) } :all

  /* gp addresses small globals with a signed 12-bit offset, so it must sit
     within 2 KB of them. Point it 0x800 past the start of the small data. */
  .sdata : {
    __global_pointer$ = . + 0x800;
    *(.sdata*)
  } :all

  .sbss : { *(.sbss*) *(.scommon) } :all
  .bss  : { *(.bss*) *(COMMON) } :all   /* the weights land here */

  . = ALIGN(16);
  _stack_top = . + 0x4000;       /* 16 KB stack, label its top */
}
