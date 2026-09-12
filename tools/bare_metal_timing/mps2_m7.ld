/* Minimal linker script for QEMU mps2-an500 (Cortex-M7) */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}

ENTRY(Reset_Handler)

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text : { *(.text*) *(.rodata*) } > FLASH
  .data : {
    _sdata = .;
    *(.data*)
    _edata = .;
  } > RAM AT > FLASH
  _sidata = LOADADDR(.data);
  .bss : {
    _sbss = .;
    *(.bss*) *(COMMON)
    _ebss = .;
  } > RAM
  . = ALIGN(8);
  end = .;
  _end = .;
  ._user_heap_stack : {
    . = ALIGN(8);
    . = . + 0x10000; /* 64KB heap+stack region */
    . = ALIGN(8);
  } > RAM
  _estack = ORIGIN(RAM) + LENGTH(RAM);
}
