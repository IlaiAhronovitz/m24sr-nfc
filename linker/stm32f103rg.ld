/* STM32F103RG (XL-density): 1 MiB flash, 96 KiB SRAM (RM0008 section 3.3).
 * The first 4 KiB of RAM hold the report block the host reads over SWD. */
MEMORY
{
  FLASH  (rx) : ORIGIN = 0x08000000, LENGTH = 1024K
  REPORT (rw) : ORIGIN = 0x20000000, LENGTH = 4K
  RAM    (rw) : ORIGIN = 0x20001000, LENGTH = 92K
}

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .vectors : { KEEP(*(.vectors)) } > FLASH

  .text : {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  .ARM.exidx : { *(.ARM.exidx*) } > FLASH

  _sidata = LOADADDR(.data);
  .data : {
    . = ALIGN(4);
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT> FLASH

  .bss (NOLOAD) : {
    . = ALIGN(4);
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  .report (NOLOAD) : { KEEP(*(.report)) } > REPORT

  ASSERT(_estack - _ebss >= 8K, "stack headroom below 8 KiB")
}
