/* gpt3_bm.ld for gpt3_kvown.c, plus two layout pins so only main differs from the gpt3_hs2_* binaries:
 * sgemv_n/sgemv_t start where gpt3_hs2_* have them, and kv_init + kv_dup sit after the read-only data. */

OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS
{
  all PT_LOAD FLAGS(7);
}

SECTIONS
{
  . = 0x80000000;

  .text : {
    *(.text._start)
    *(EXCLUDE_FILE(*sgemv_n.o *sgemv_t.o) .text*)
    . = MAX(., 0x2f60);          /* sgemv_n at 0x80002f60, as in gpt3_hs2_* (MAX: sizes shrink only after relaxation) */
    *(.text*)
  } :all

  .rodata : { *(.rodata*) *(.srodata*) } :all
  .eh_frame : { *(.eh_frame) } :all
  .kvinit : { *(.kvinit) *(.kvdup) } :all  /* init only, in the gap below .data */
  .data   : { *(.data*) } :all

  .sdata : {
    __global_pointer$ = . + 0x800;
    *(.sdata*)
  } :all

  .sbss : { *(.sbss*) *(.scommon) } :all
  .bss  : { *(.bss*) *(COMMON) } :all

  . = ALIGN(16);
  _stack_top = . + 0x4000;
}
