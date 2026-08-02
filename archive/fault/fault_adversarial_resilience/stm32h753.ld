/*
 * Linker Script for STM32H753XI Cortex-M7
 * Memory Architecture: 2MB Internal Flash, 512KB Primary AXI_SRAM
 */

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
  SRAM  (xrw) : ORIGIN = 0x24000000, LENGTH = 512K  /* Primary AXI_SRAM domain */
  SDRAM (xrw) : ORIGIN = 0xD0000000, LENGTH = 32M   /* FMC Bank 2 External SDRAM */
}

ENTRY(Reset_Handler)

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  .data : AT(_etext)
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > SRAM

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > SRAM

  /* Define Top of Stack at end of primary AXI_SRAM */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + 0x1000; /* Minimum 4KB stack */
    . = ALIGN(8);
  } > SRAM

  _estack = ORIGIN(SRAM) + LENGTH(SRAM);

  /* Map large telemetry buffers into uninitialized SDRAM */
  .sdram_data (NOLOAD) :
  {
    . = ALIGN(4);
    *(.sdram_data)
    *(.sdram_data*)
    . = ALIGN(4);
  } > SDRAM
}
