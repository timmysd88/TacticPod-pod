SECTIONS
{
  .ble_reset (NOLOAD) :
  {
    . = ALIGN(4);
    KEEP(*(.ble_reset))
  }
} INSERT AFTER .bss;
