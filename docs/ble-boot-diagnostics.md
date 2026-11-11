# BLE Phase 2 startup diagnostics

`tacticpod_reusable_ble_boot_diagnostics` is an opt-in application-only build.
The normal default, USB and BLE environments do not include these checkpoints.
It preserves the BLE reset marker and does not reserve bootloader, SoftDevice,
or retention-register space. A separate 16-byte NOLOAD record follows the
8-byte BLE marker and precedes the heap. It records a value and its complement
for both the current and immediately preceding application boot.
The linker may place the BLE marker at a different address in different
firmware builds; neither its old address nor retained contents should be
assumed to survive an application update.

The preinit hook runs after the reset handler, `SystemInit` and C runtime setup,
but before C++ global constructors. It copies a valid current checkpoint to
the previous slot and starts a new current slot. Checkpoint values indicate
the operation **about to run**, not proof that it completed:

| Value | Last checkpoint | Following work until the next checkpoint |
|---|---|---|
| 0 | Unknown or invalid | No trustworthy retained value |
| 1 | Preinit | Global constructors, framework initialization, task creation and TinyUSB initialization |
| 2 | Setup entry | Entry to application setup |
| 3 | Battery | Battery initialization |
| 4 | Clock | Clock, observation and GPIO initialization |
| 5 | Serial | USB CDC `Serial.begin` |
| 6 | GPS | GPS receiver initialization |
| 7 | Flash | SPI and release from deep power-down |
| 8 | IMU | IMU initialization |
| 9 | Recorder | Legacy recorder initialization |
| 10 | TCB | TCB initialization and final startup indication |
| 11 | Ready | End of setup reached; subsequent application execution |

Once setup finishes and a USB serial host connects at 115200 baud, an idle
foreground slice sends `BOOT prev=N current=N`. The `BOOT STATUS` command
requests the same report again. Reports wait for USB buffer space and avoid
active capture and export paths; they do not wait for a host during startup.
The record is diagnostic evidence only and does not modify recorded flash.

## Limits and safety

SRAM retention across reset, power loss, and bootloader execution is not
guaranteed. The bootloader may use or clear this RAM, and a valid-looking
record can be stale. Complement checks detect many incomplete writes, not all
corruption; a failure during an update can make the record invalid. Only the
immediately preceding boot is retained, and subsequent resets replace it.
Power removal is therefore not a reliable way to recover evidence.

The report is available only if a later boot reaches setup completion and USB
serial works. A bootloader that never jumps to the application, a reset before
preinit, a corrupt vector, a fault in `SystemInit`/C runtime, loss of retained
RAM, repeated failure, or a broken USB path may leave no observable report.
The preinit checkpoint does not identify which constructor or framework step
failed. A checkpoint does not capture an exception, reset reason, or prove
that a preceding peripheral initialized successfully. Failures after `Ready`
are not localized. SWD or another independently readable output is needed
when no later application serial session is possible.

Build and host verification only; do not upload without a separate approved
hardware procedure. On an approved test, use only the verified diagnostic
application ZIP, preserve the rollback ZIPs and recordings, observe whether
normal USB enumerates, and capture the `BOOT` line or `BOOT STATUS` response.
If the device stays in DFU, stop and record that observation: this mechanism
cannot read retained RAM through the bootloader's DFU interface.
