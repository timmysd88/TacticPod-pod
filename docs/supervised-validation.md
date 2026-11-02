# Supervised hardware validation procedure

Nothing here runs automatically. Every step is performed by the owner, and the
firmware upload, battery connection and recording steps each need explicit prior
approval. Nothing below claims a result: record measured values in the report
template at the end. Background: [seeed-confirmation.md](seeed-confirmation.md).

## Stop immediately if

Battery or board becomes hot, swells or smells; VBAT reads above 4.25 V or below
3.0 V; polarity is in doubt; the USB drive/port behaves unexpectedly after upload;
any step result differs from "Expect". Disconnect the battery, then USB, and report.

## 0. Preconditions

- Firmware artifact from a green CI run of `tacticpod_reusable_usb`; verify
  `SHA256SUMS.txt` against the downloaded `.hex`/`.zip`.
- Export all existing TPX3 sessions first (`TCB READ`) and keep copies. The
  procedure never erases flash.
- Multimeter (verified on a known source), the 300 mAh LiPo with polarity checked
  against the XIAO BAT pads, optionally a USB current meter and a meter for series
  current measurement.
- Bootloader recovery known: double-tap RESET shows the UF2 drive. Confirm
  `INFO_UF2.TXT` identifies the factory bootloader; the bootloader is never modified.

## 1. USB-only upload (approval required, battery absent)

1. Approve upload explicitly. Install the verified build through the existing UF2
   bootloader (double-tap RESET, copy the UF2/DFU package). Lab firmware flashing
   stays disabled.
2. Expect: re-enumerates as a serial port; no LED recording pattern; no recording
   started by reset, upload or USB insertion.

## 2. USB diagnostics

Send `TCB STATUS`, `BAT STATUS`, `GPS STATUS`, `RECORD STATUS`. Expect: recorder
Idle, existing sessions listed with unchanged counts, `mode=`/`free_sectors=`
reported, battery calibration `uncalibrated` (RAM-only), no ADC errors. Then
unplug/replug USB and press RESET: still no recording.

## 3. Multimeter voltage comparison (battery connected, approval required)

1. Connect the LiPo (observe polarity); keep USB for the serial link.
2. Measure directly at the cell terminals with the meter. The 1M/510k divider is
   high impedance: do not probe the P0.31 junction.
3. `BAT STATUS` for the raw reading, then `BAT CAL <measured mV>` (RAM-only), then
   `BAT MODE CELL`. Expect corrected voltage within meter and ADC tolerance; record
   both values. While USB charges the cell, VBAT is the charging voltage, so note
   the power source in `BAT STATUS`.
4. Repeat after any reset: calibration is lost by design.

Charging limits: P0.13 left as input gives nominal 50 mA per Seeed (≈0.17 C for
300 mAh). Do not leave charging unattended on the first runs; check cell and board
temperature by touch at intervals; never charge a cell that reads below its
protection voltage or looks damaged.

## 4. Button-controlled recording (USB disconnected, battery only)

Calibrate first (section 3), then unplug USB. Start outdoors with clear sky.

| Step | Action | Expect |
| --- | --- | --- |
| 4.1 | Tap D0 for <1 s | No recording; at most no LED |
| 4.2 | Hold D0 3 s | Two blue flashes, then a longer blue confirmation; recording starts |
| 4.3 | Observe up to 2 min | Blue flash every ~3 s; alternating blue/green once a fresh fix exists; blue again if the fix is lost |
| 4.4 | Hold D0 3 s alone | Recording continues (standalone hold must not stop) |
| 4.5 | Tap, release, press again within 5 s, release before 3 s | Stop cancelled; recording continues |
| 4.6 | Tap, release, press again, hold 3 s | Two green flashes, LED off, standby |
| 4.7 | After standby, press/hold D0 3 s again | A new session starts; old sessions untouched |

Low battery (orange flash) is only expected with a calibrated, fresh reading under
the warning threshold; do not force it by over-discharging the cell. A red pattern
means start failure or fault: record the `TCB STATUS` text. Keep the first run to
about 120 s.

## 5. Standby current (measure, do not assume)

Place a meter in series with the battery lead (use a range that tolerates a
start-up surge, or a shunt/bypass during the LED and GPS-start period). After
stopping a recording, wait for standby, then record the stable current. In standby
the GPS module and the sense divider remain powered; System ON sleep is used, not
System OFF. Report the measured value; no figure is claimed in this repository.

## 6. TPX3 export and verification

Reconnect USB, `TCB STATUS`, then `TCB READ` the new session and decode it with the
TPX3 decoder/Lab. Expect: finalised session, valid CRCs, GPS records near 10 Hz and
IMU samples near 100 Hz over the elapsed time, old sessions byte-identical to the
step 0 exports. Record the counts.

## 7. Recovery checks

Press RESET: no recording. Double-tap RESET: UF2 drive appears. Reconnect USB while
in standby: if the port is absent, press RESET once (USB wake is not claimed).

## Report template

Firmware SHA-256 / build run; step results (pass/fail); measured cell voltage vs
`BAT STATUS`; standby current; temperatures; LED observations; session counts and
rates; anything unexpected.

Pass: all "Expect" rows met, sessions intact, no thermal or voltage anomaly.
