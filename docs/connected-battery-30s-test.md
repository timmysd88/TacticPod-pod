# Proposed connected-battery 30-second test — 1 November 2026

**Prepared only. No recording is authorised or initiated by this document.**
Use the existing installed `tacticpod_reusable_usb` application. No build/upload,
bootloader change, erase, battery calibration/threshold/mode change or GPIO
charging change is part of this procedure. Separate recording approval is required.

## Preconditions

Review the [current diagnostic report](../logs/connected-battery-validation/REPORT.md)
and its GO/NO-GO decision first. Read-only serial diagnostics cannot qualify cell
condition, polarity, charger current/temperature, startup sense-pin exposure or
electrical handover. Resolve/review those outstanding physical qualifications
before treating this as an approved battery trial.

The expected baseline is four complete sessions **1, 3, 12, 58**, allocated through
`0x03BFFF`, with next append **0x03C000**, address-derived session **61**.
The pre-test TPX3 baseline must be current and all four sessions independently
verified; any intervening recording invalidates this expectation.

Identify the application USB port by VID:PID `2886:8044`, serial
`0E8A89B7289430BD` (currently COM8). Use **115200 baud** throughout; no reset or
1200-baud touch. Keep the device supervised and the existing battery connection
undisturbed throughout the proposed trial.

## Only after separate approval

1. With USB connected, read `RECORD STATUS`, `OBS STATUS`, `COMP STATUS`,
   `TCB STATUS`, `BAT STATUS`, `GPS STATUS`. Require legacy STOPPED,
   OBS capture/RAM_fast_imu OFF, COMP capturing/fault zero and queues empty,
   TCB Idle (`state=1`), `lock=0`, `error=none`, zero rejected/missed/full/CRC/
   incomplete counters and the expected append address. Finish all diagnostics
   and export activity. If cell mode is armed but untrusted/low, stop; do not
   change mode to bypass admission. If unarmed, explicitly acknowledge that no
   automatic low-voltage stop is active. This procedure does not arm it.
2. Send exactly:

   ```text
   TCB DURATION 30
   TCB STATUS
   BAT STATUS
   TCB START
   ```

   START is sent **once**. Wait for `TCB STARTED`, following the scan; verify
   session61, duration30/drain60, no error/fault/loss. Capture timing begins at
   STARTED, not when START was sent. A refusal or unexpected session ends the
   attempt; preserve evidence without retry.
3. About two seconds after STARTED, physically disconnect USB. Record the host
   time of STARTED, disconnection and later reconnection. Do not reset or remove
   battery power. No serial command is required while disconnected.
4. Firmware should stop capture at 30 seconds and finalise within a further
   60 seconds. Reconnect USB **at least 100 seconds after STARTED**, allowing a
   margin beyond the 90-second deadline. Supervise physical condition throughout.
5. At 115200 baud, repeat the six status commands from step1. Require clean TCB
   Idle/session61/lock0/errornone and zero fault/loss counters, COMP drained, other
   recorders stopped. A normal read-only legacy index rescan may briefly report
   SCANNING: wait for completion. New boot evidence, early stop, drain timeout,
   incomplete session or faults means failed test; no second START or erase.
   A manual STOP, if separately needed/authorised, does not constitute a passing
   autonomous-finalisation test.

## Two independent exports and verification

Release the monitor. Re-identify the current port; substitute it if COM8 changed.
Use a new evidence directory and exclusive new filenames. Example PowerShell
commands, prepared here and **not executed for a new recording**:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/tcb3_download.py --port COM8 --output logs/connected-battery-validation/test30-after-1.tpx3
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/tcb3_download.py --port COM8 --output logs/connected-battery-validation/test30-after-2.tpx3
Get-FileHash logs/connected-battery-validation/test30-after-1.tpx3,logs/connected-battery-validation/test30-after-2.tpx3 -Algorithm SHA256
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/tcb3_decode.py logs/connected-battery-validation/test30-after-1.tpx3 > logs/connected-battery-validation/test30-after-1.json
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/tcb3_decode.py logs/connected-battery-validation/test30-after-2.tpx3 > logs/connected-battery-validation/test30-after-2.json
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/verify_tcb3_append.py --before logs/connected-battery-validation/baseline-1.tpx3 --after logs/connected-battery-validation/test30-after-1.tpx3 --duration 30
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/verify_tcb3_append.py --before logs/connected-battery-validation/baseline-1.tpx3 --after logs/connected-battery-validation/test30-after-2.tpx3 --duration 30
```

Each download sends only `TCB READ`; send no other commands during binary output.
Require equal hashes and independent packet/container/session validation of both
files: no root/export/session issues, valid page commit seals/CRCs, block CRCs,
sequences, metadata and footer accounting. Require the entire four-session old
image byte-identical, exactly one new complete session61, minimum new address
0x03C000, no loss/rejection/missed/full/CRC/incomplete counters and clean footer.
Inspect each previous session independently by decoding its isolated sectors,
as implemented for both baseline exports in `read_only.py`.

Expected counts are approximately **3000 IMU / 300 RMC / 30 GGA**; verifier
tolerances are ±2/±2/±1, with duration30.000–30.050s and IMU gap checks.
Review GPS fix validity and unique advancing UTC separately from message counts.
The 30-second conservative admission budget is 45 sectors (184320 bytes), while
actual allocation follows compression. Writes remain below 0xFFF000; this is
not a fixed 45-sector physical fence. No prior sector is reused or erased.

Continuous recorded samples across USB loss and a clean footer after 30 seconds
support battery operation during that interval. USB serial has no telemetry while
disconnected and no persistent boot counter; this cannot prove absence of a reset
between finalisation and reconnection. VBUS presence never proves load sharing or
charging safety. Finalisation leaves the board/GPS powered and consuming energy.
