SECTIONS
{
  .boot_diagnostics (NOLOAD) :
  {
    . = ALIGN(4);
    KEEP(*(.boot_diagnostics))
  }
} INSERT AFTER .ble_reset;
