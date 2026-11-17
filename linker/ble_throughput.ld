SEARCH_DIR(.)
GROUP(-lgcc -lc -lnosys)

MEMORY
{
  FLASH (rx) : ORIGIN = 0x27000, LENGTH = 0xED000 - 0x27000
  /* BLE-only reserve for MTU 247, the eight-entry HVN queue and event budget.
     Bluefruit still checks sd_ble_enable's actual RAM requirement at startup. */
  RAM (rwx) : ORIGIN = 0x20008000, LENGTH = 0x20040000 - 0x20008000
}

SECTIONS
{
  .svc_data :
  {
    . = ALIGN(4);
    PROVIDE(__start_svc_data = .);
    KEEP(*(.svc_data))
    PROVIDE(__stop_svc_data = .);
  } > RAM
  .fs_data :
  {
    . = ALIGN(4);
    PROVIDE(__start_fs_data = .);
    KEEP(*(.fs_data))
    PROVIDE(__stop_fs_data = .);
  } > RAM
} INSERT AFTER .data;

INCLUDE "nrf52_common.ld"
