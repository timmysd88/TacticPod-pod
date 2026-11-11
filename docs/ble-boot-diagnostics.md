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

## Phase 2 GATT initialization correction

The first diagnostic hardware test reached `BOOT prev=0 current=11`; a later
double-tap reached BLE initialization and reported
`BLE ERROR: GATT characteristic initialization` before advertising.
The correction changes only the Data notification security metadata, using
Bluefruit's read security setting to make its CCCD writable. It also reports
the individual characteristic name and returned error code. It does not change
button timing, recording, UUIDs, packet formats, or SoftDevice configuration.

Corrected application-only diagnostic package, archived without overwriting the
original diagnostic or recovery firmware:

```text
C:\Tacticpod\firmware-stage\phase2-gatt-fix-2026-11-11-7a143f5\tacticpod-ble-phase2-gatt-fix-fd499881.zip
SHA-256: fd499881253343f18215f3c24859aad16a301234971842bd2ee5d0db5f90ab1a
Application bytes: 317576
DFU CRC16: 0xcb97
```

The corrected diagnostic uses 141000 bytes of `.data` + `.bss`, unchanged from
the initial diagnostic build, plus its separate retained markers. Application
flash increased by 264 bytes from the initial diagnostic build. Linker regions
and stack/heap boundaries are unchanged.

Validation passed for all seven PlatformIO environments and all 44 regression
scripts, plus the separate compiled-startup test. The linked Bluefruit GATT
regression rejects the original Data permissions and accepts all five corrected
characteristics in both BLE environments. ELF/vector, retained-memory placement,
application-only DFU contents and CRC checks passed. The original diagnostic,
Phase 1 BLE and USB recovery archive hashes remain unchanged.

On a separately approved application-only upload of the corrected build:

1. Verify the archived ZIP hash and device identity; confirm recording stopped
   and TCB healthy before the single-bank application-only DFU operation.
2. Verify USB enumeration and `BOOT STATUS`, `RECORD STATUS`, and `TCB STATUS`.
3. Keep a serial capture open and double-tap while idle. Require the advertising
   message, blue advertising indication, and no GATT initialization error.
4. With nRF Connect or the existing app connection workflow, confirm `TacticPod`
   and the unchanged service UUID. Inspect all five characteristics, read
   protocol version `[2, 0]` and the six-byte battery value, and enable Data
   notifications through the CCCD before sending a valid LIST.
5. Confirm the notification stream and UUIDs match the protocol. Do not start a
   recording, erase/reset recordings, pair/bond, or upload fallback firmware.
   If initialization still fails, capture the characteristic name/error code
   and stop; do not retry the upload automatically.
