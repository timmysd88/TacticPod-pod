# Switchless reusable firmware — first-test procedure for review

Firmware environment: **tacticpod_reusable_usb**. No new firmware behaviour was
needed: the corrected reusable application already supports USB-started timed
recording independent of USB. New host tools and offline regression cases verify
the workflow. No upload, serial-port access, battery connection or physical flash
operation was performed for this preparation.

**Software GO for review and USB-only qualification. Electrical LiPo operation
remains unqualified.** This procedure is conditional on separately reviewing the
remaining battery/power risks; it does not clear the P0.14/P0.31 reset interval,
charge-current accuracy, polarity or source handover. Keep the cell disconnected
until the connection stage is explicitly approved. See
[stock-board startup assessment](stock-board-battery-startup.md) and the existing
[battery checklist](battery-bench-checklist.md).

## Verified behaviour and limits

- P0.14's corrected variant clears LOW before OUTPUT, ahead of setup. The generic
  core's preceding all-HIGH latch preload and earlier bootloader/reset states
  remain disclosed; no bootloader patch was made.
- setup never waits for Serial. IMU, flash read-only recovery and GPS reception
  initialise without a monitor. GPS startup configuration progresses without USB
  after valid NMEA is received; no communication within 30 seconds is reported
  later, rather than silently assumed configured. Only GPS RAM is configured.
- Nothing automatically records at boot, reset or USB connection. `TCB START`
  must be issued after preflight. USB loss does not call STOP or reset in the
  recording code. Adequate continuous physical power is still required.
- `TCB DURATION 1` through `TCB DURATION 1800` select capture seconds while idle;
  default 120. Duration starts at `TCB STARTED`, after scan/admission, not at the
  command's initial response. Automatic capture cutoff and a further 60-second
  finalisation deadline run without USB. Duration/settings are RAM-only.
- No USB connection automatically starts a second session, resumes a torn session
  or erases. A real reset loses RAM configuration and in-flight samples. Recovery
  preserves valid committed pages and appends later; it cannot recreate lost data.
- P0.13 remains INPUT/no pulls for proposed nominal 50mA. Physical charging and
  the exact cell specification have not been independently qualified.
- Low-battery software needs valid, fresh, calibrated, explicitly armed readings.
  `source=ADC_UNVALIDATED` remains the source label even after one-point calibration.
  Policy `trusted=1` means eligible for decisions, not certified voltage accuracy.
- `BAT MODE USB` is the unarmed policy mode, **not a USB-power requirement**. It
  permits timed logging with USB absent but has no voltage-triggered warning/stop.
  Never claim this is an armed battery-protection mode. Invalid/stale/unavailable
  readings cannot trigger false low-voltage shutdowns. Existing armed low-voltage
  thresholds/hysteresis and finalisation remain implemented and tested.
- Removing USB does not switch the device off while a battery supplies it. There
  is no software power switch, deep-sleep shutdown or connected record button.
  An idle unit still consumes power. `TCB STOP` finalises; it does not disconnect
  the cell or disable the autonomous charger.

## Prepared artifact and offline commands

Prepared archive:
`logs/switchless-battery-review/tacticpod_reusable_usb-switchless.zip`

SHA-256:
`ffee03178a49e2b77130da9afdefd5f5c358c6b22ecb3e1e249b2157ca540c67`

The application is byte-identical to the corrected stock-board build. It preserves
TCB3 3.2.0, V1/V2 compatibility, TPX3 export, raw sensor precision and all existing
flash guards. It has reusable recording capability, not a physical-write-disabled
test mode. Default firmware remains TCB3-disabled and unchanged.

Build/test commands, already run without hardware:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e tacticpod_reusable_usb -e seeed_xiao_nrf52840_sense
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" logs/switchless-battery-review/run_validation.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/gps/run_tests.py
```

Do not execute the following upload/connection/recording steps until approved.

## Stage A — USB-only upload and read-only preflight

1. Keep the battery absent. Save/open no second serial monitor. Verify the archive:

   ```powershell
   Get-FileHash logs/switchless-battery-review/tacticpod_reusable_usb-switchless.zip -Algorithm SHA256
   ```

   Abort on a hash mismatch. Confirm every recorder is stopped before entering DFU.
2. Use the established nrfutil **application ZIP** method. While USB-only, enter
   the existing bootloader by the stock reset procedure, identify its current COM
   port, and run this command with that actual bootloader port (COM7 is the prior
   example, not a guaranteed port):

   ```powershell
   & "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" .pio-core/packages/tool-adafruit-nrfutil/adafruit-nrfutil.py dfu serial -pkg logs/switchless-battery-review/tacticpod_reusable_usb-switchless.zip -p COM7 -b 115200 --singlebank
   ```

   This does not install a bootloader. No automatic upload/retry script is invoked.
   A failed upload is a stop condition, not permission to erase or retry blindly.
3. Open the application port (previously COM8) at **115200 baud**, never 1200 during
   testing. Example monitor command:

   ```powershell
   & "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m serial.tools.miniterm COM8 115200
   ```

   Wait for flash scan completion and GPS startup profile result. Send the exact
   existing commands below, one at a time, waiting for each response:

   ```text
   IMU OFF
   GPS OFF
   RECORD STATUS
   OBS STATUS
   COMP STATUS
   TCB STATUS
   BAT STATUS
   DIAG ID
   GPS STATUS
   ```

   Require RECORD STOPPED, OBS capture OFF/RAM_fast_imu OFF, COMP capturing=0 and
   empty queues, TCB idle/not locked, JEDEC 0B4018, measured 16MiB SFDP fingerprint
   and no protection/fault changes. The reused flash guard requires SR1=00,
   SR2=02. Confirm the automatic RAM-only 10Hz GPS profile is applied/verified.
   Wait for all diagnostic output to finish. Do not issue manual GPS TEST/RESTORE.
4. For explicit functional checks while idle, use `IMU ON` briefly, then `IMU OFF`;
   verify live sensor values and no IMU initialisation error. This stream is 10Hz;
   the binary recording is 100Hz. `GPS RATE` runs for 15 seconds and should show
   RMC primary near 10Hz and independent GGA near 1Hz. Valid navigation requires
   valid fix and advancing UTC evidence; no fix does not invalidate stored raw
   observations. Wait for `GPS RATE END` before recording or export.
5. Close miniterm (Ctrl+]) to release COM8. Download a pre-test binary baseline:

   ```powershell
   & "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/tcb3_download.py --port COM8 --output logs/switchless-battery-review/before.tpx3
   & "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/tcb3_decode.py logs/switchless-battery-review/before.tpx3 > logs/switchless-battery-review/before.json
   ```

   The downloader sends only `TCB READ`, stores raw bytes and releases serial. It
   refuses an existing output file, preserves partial failed downloads and does
   not retry. Decoder exit code alone is not an integrity verdict: require empty
   root/export/session issues and complete expected sessions. Compare the three
   sessions/committed prefix against the archived Phase3B10 export. Currently
   expected IDs are 1,3,12, occupied prefix through 0x038FFF, next session58 at
   0x039000. Any discrepancy needs review before START; do not erase to fix it.

## Stage B — conditional battery connection and stopped checks

This is the unresolved electrical stage, not authorised by the software result.
Do not connect now. After separate review: keep all recorders stopped, disconnect
USB before fitting a verified-polarity, insulated protected-cell connection to
the stock BAT+/GND nets. No connector polarity, existing adapter or soldering
permission is assumed. If the required safe stock-pad connection is unavailable,
stop; firmware cannot supply the missing electrical connection.

Cell connection may immediately power the board. Do not press reset to start
recording. Let it boot without a monitor, then reconnect USB at 115200 and repeat
the stopped-state, GPS, IMU and flash checks above. Attaching USB can start the
hardware charger immediately, independently of commands; that charging risk is
part of this stage's separate review. Abnormal heat, unstable enumeration or
reset is a stop condition.

Choose the battery policy honestly while idle:

- Without an independently measured voltage, use/retain **`BAT MODE USB`** and
  verify `cell=0 calibrated=0 trusted=0`. This short engineering capture relies
  on its ten-second duration limit, not voltage-triggered finalisation. Do not
  fake `BAT CAL` or treat nominal readings as accurate. This is not qualified
  unattended battery operation.
- Only if a real stable meter reference and hardware qualification become
  available, use existing `BAT CAL <actual_measured_mV>` then `BAT MODE CELL`.
  Wait for three fresh valid samples and check trusted=1/no stop. Never enter a
  made-up 3700/4200 value. If CELL is selected but remains untrusted, START
  refuses; do not bypass that refusal by pretending the reading was calibrated.

BAT STATUS's usb_vbus is actual VBUS detection, distinct from opening a monitor;
cell=1 is operator arming, not automatic physical cell detection.

## Stage C — one ten-second recording, no retry

1. Reopen the application monitor at 115200. Confirm the baseline is saved and all
   diagnostics stopped. Send:

   ```text
   TCB DURATION 10
   TCB STATUS
   BAT STATUS
   TCB START
   ```

   Issue START **once**. Wait past `START scanning` for **`TCB STARTED`**, require
   session58 given the unchanged baseline, duration_s=10, drain_s=60, lock=0,
   error=none and zero rejected/missed/full/CRC/incomplete counters. If START
   refuses or the session ID differs, preserve everything and stop.
2. About two seconds after STARTED, disconnect USB while the already-connected
   battery supplies power. Do not open a 1200-baud port, reset, change wiring or
   remove battery power. No serial command is needed while disconnected.
3. Capture ends automatically at ten seconds; firmware may need up to a further
   60 seconds to finalise or fault. Wait until at least **80 seconds after STARTED**
   before reconnecting USB. Keep the device supervised. No second START, manual
   power cut or erase is part of this test.
4. Reconnect at 115200. Send `TCB STATUS`, `RECORD STATUS`, `OBS STATUS`,
   `COMP STATUS`, `BAT STATUS`, `GPS STATUS`. Require TCB STOPPED/finished (store
   state=1/Idle after successful footer verification), session58,
   lock=0/error=none; other
   recorders stopped; compressor drained; fault/loss counters zero. Reconnection
   prints a menu/retained diagnostics; it does not start a new recording.

   Continuous power means reconnection is **not a reboot recovery**. A new boot
   or interrupted session is a failure to investigate, even if prior committed
   blocks remain recoverable. A complete footer and continuous ten-second sample
   stream demonstrate the recorded interval; without a persistent boot counter
   they cannot prove no reset occurred after finalisation and before reconnection.

## Stage D — duplicate export and offline verification

Close/release the monitor before each downloader. Do not send any other command
while binary export is active. Download twice to new filenames:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/tcb3_download.py --port COM8 --output logs/switchless-battery-review/after-1.tpx3
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/tcb3_download.py --port COM8 --output logs/switchless-battery-review/after-2.tpx3
Get-FileHash logs/switchless-battery-review/after-1.tpx3,logs/switchless-battery-review/after-2.tpx3 -Algorithm SHA256
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/tcb3_decode.py logs/switchless-battery-review/after-1.tpx3 > logs/switchless-battery-review/after-1.json
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/verify_tcb3_append.py --before logs/switchless-battery-review/before.tpx3 --after logs/switchless-battery-review/after-1.tpx3 --duration 10
```

Require identical export hashes; all three old sessions and their reconstructed
committed flash prefix unchanged; exactly one new complete session; valid page
CRC/commit seals, block CRCs, TPX3 packet CRCs, sector/page/block sequences and
footer accounting; no loss/rejected/missed/full/incomplete counters. Expected
observations are about 1000 IMU /100 RMC /10 GGA. The verifier allows boundary
effects (IMU/RMC ±2, GGA ±1); it does not generate or trim samples. Review GPS
fix/UTC validity separately; message count alone does not prove 10Hz navigation.

The first new session's minimum write address is 0x039000. The reusable hard
write window ends at 0xFFEFFF; 0xFFF000-0xFFFFFF remains protected. Every newly
allocated sector/page is blank-checked and existing sessions never resumed or
overwritten. Ten seconds has a conservative admission budget of 17 sectors;
this is not a separate per-session physical address fence. Modelled use was five
sectors 0x039000-0x03DFFF. Actual data/compression determines physical use.

Any reset, loss, CRC/commit/sequence error, short/interrupted export, flash fault,
unexpected session, early stop or drain timeout: preserve data/logs, no retry or
erase. `TCB STOP` can request finalisation if still running; do not treat that as
a successful autonomous-stop test. Stop all host tools to release COM8. Leaving
the battery attached still powers the unit; finalisation is not power-off.
