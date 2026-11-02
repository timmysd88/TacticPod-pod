# Soshine 602030 / XIAO Sense - staged connection and validation checklist

> **Update 2026-11-02:** Seeed Technical Support has confirmed the P0.14/P0.13/bootloader behaviour (see [seeed-confirmation.md](seeed-confirmation.md)). The bootloader-interval concern and "NO-GO for LiPo connection" below are historical; battery testing now follows [supervised-validation.md](supervised-validation.md) and still needs explicit approval. No electrical measurement by this project is implied.

This is a **future procedure for review**, not permission to connect the pack or
upload firmware now. The present 300mAh pack remains disconnected. See
[reusable-prototype.md](reusable-prototype.md) for code behaviour, source evidence,
the boot sense-pin issue and qualification limits.

## Gate 1 - identify and inspect without power

1. Confirm the actual board is the original XIAO nRF52840 Sense, not a Plus or
   clone, and match its revision to the appropriate manufacturer schematic/back
   view. Identify the underside battery pads by their **BAT+/BAT- or +/− markings
   and electrical nets**, not a left/right description from another board image.
2. Record the Soshine pack label and supplier specifications. Verify this is a
   single-cell pack intended for 4.2V charging; confirm protection and the allowed
   charge current for this exact pack. The reported 1.2A rating is not adopted
   as an independently verified charging specification. Do not bypass its PCM.
3. With USB and battery disconnected from the board, verify the chosen BAT- pad
   has ground continuity. Use the matching schematic/markings to identify BAT+
   (VBAT). If markings/net identity are ambiguous, stop rather than infer polarity.
4. Measure the **pack's accessible output/connector** with a meter: the meter must
   show positive voltage with red probe on the intended PACK+ contact and black
   on PACK-. Record the pin order. Wire colour, JST appearance and connector fit
   alone are insufficient. Avoid bridging adjacent contacts with a probe.
5. Inspect for swelling, damage, exposed foil, crushed insulation or protection
   board damage. A protected pack does not make reversed wiring or heating safe.

Exact intended wiring, after all gates pass:

| Verified pack net | Verified XIAO net |
| --- | --- |
| PACK+ (positive protected output) | Underside BAT+ / VBAT positive pad |
| PACK- (negative protected output) | Underside BAT- / ground pad |

Do not connect the pack to the USB 5V header, regulated 3V3 output, GPIO, flash CS
or ADC input. Keep the existing proven GPS/flash wiring unchanged and verify their
supply rails under battery power later. There is no assumed connector polarity
or already-connected momentary switch. Do not solder directly to pouch foil.
Any adapter/soldering must be done with both sources disconnected and insulated
strain-relieved leads; no adapter is assumed purchased or available.

## Gate 2 - USB-only firmware and sense-pin qualification

1. Obtain separate approval for the reviewed dedicated ZIP and verify its hash.
   Upload with the battery still absent. Confirm the reusable marker, no automatic
   capture, RECORD STOPPED, OBS OFF, COMP OFF and TCB idle.
2. Back up/read all three sessions and compare against the current 255424-byte
   TPX3 archive before any further recording. Require identity 0B4018, 16MiB,
   expected status/SFDP, GPS RAM profile and recovered append 0x039000.
3. Read `BAT STATUS`. Expect cell=0, calibrated=0 and trusted=0. Any nominal voltage
   on USB is **not proof of a connected battery**; do not arm cell mode or calibrate
   against an assumed 4.2V. Real hardware readings remain unvalidated.
4. Instrument or otherwise qualify P0.14/P0.31 across reset, bootloader, core init
   and application startup against the actual board circuit and pin limits.
   The corrected reusable project variant preloads P0.14 LOW before OUTPUT;
   the vendor/default variant still drives it HIGH earlier. See
   [battery-startup-safety.md](battery-startup-safety.md). Resolve reset and
   bootloader intervals before LiPo attachment; do not assume the corrected
   variant controls the earlier power ramp or bootloader. If this cannot be established,
   remain USB-only. No bootloader change was made by this task.
5. Check ADC conversion timing and noninterference with IMU/GPS on hardware before
   relying on voltage-triggered finalisation. Register-model tests are not voltage
   accuracy or scheduler measurements.

## Gate 3 - first pack connection and charging, only after clearance

1. Stop every recorder and remove USB power before fitting/soldering the verified
   polarity connection. Perform a final independent polarity check, insulate and
   secure the leads, and have a safe way to disconnect the pack. Connection may
   power the board immediately; no power switch is assumed. Never use RESET as
   a recording STOP or battery disconnect.
2. Observe the first cell-powered boot with recording OFF. Check for abnormal heat,
   repeated resets or unstable supply rails. Do not start a session. Verify MCU,
   GPS and flash supply behaviour against their actual requirements before logging.
3. After stable stopped operation, attach USB for diagnostics/charging observation.
   The firmware leaves the charge-current selection as high-impedance input,
   nominal low-current mode; no high-current or charge-register command is added.
   Measure actual pack current and voltage, including while the system runs.
   Do not infer charge completion/presence from an LED or USB connection alone.
4. Compare measured charge current against the verified pack specification and
   charger circuit. The nominal 50mA mode is about 0.17C for a 300mAh pack; this
   arithmetic is not an actual current measurement. Do not select 1.2A or assume
   the board can deliver it. Verify voltage regulation/termination rather than
   treating protection cutoff as normal charging control.
5. Monitor the pack directly during the first charge within its approved temperature
   limits. No pack-temperature telemetry or thermistor protection has been
   implemented/verified. Stop on unusual heating, swelling, odour, unexpected
   current, overvoltage or resets; preserve logs and do not automatically retry.

## Gate 4 - calibrate and arm software, still recording OFF

1. Measure stable voltage across the board's verified BAT+ and BAT-/GND pads with
   a trusted meter; compare the pack output separately to check lead drop. Wait for
   valid nominal ADC readings, then send `BAT CAL <measured_millivolts>`; for example,
   `BAT CAL 3972` **only if the meter actually measures 3972mV**. No canned voltage
   or simulated measurement is acceptable for hardware calibration.
2. Send `BAT MODE CELL`. Wait for at least three fresh valid samples, then check
   BAT STATUS for cell=1, calibrated=1, valid=1, trusted=1, sensible filtered voltage,
   zero unexplained timeouts and the intended limits. Verify against the meter at
   additional operating voltages before trusting a one-point gain correction.
3. The defaults are warn/stop/hard/recovery = 3600/3500/3300/3700mV. They intentionally
   precede the user-reported 2.75V discharge limit. Do not lower them to the pack's
   cutoff to extend runtime; available finalisation energy has not been measured.
4. These settings are volatile. Reset loses calibration/arming and restores absent-
   cell USB mode. Reconfigure only from verified conditions; a later field build
   needs reviewed persistent configuration/start controls. Do not hide missing
   calibration by switching to USB mode while claiming battery protection.

## Gate 5 - later bounded battery recording and power tests

1. Obtain approval for a specific bounded session and address budget. Begin with
   read-only/stopped rail measurements and power-source transition checks. No
   physical power-cut experiment is approved by this checklist.
2. For an approved capture, set duration, confirm all trust/capacity/recording
   preconditions and issue one START. Once configured, low-voltage finalisation,
   duration and watchdog require no USB/PC. Confirm real battery-only operation
   and safe completion without relying solely on USB-powered behaviour.
3. Verify two matching exports, all original sessions unchanged, counts/timestamps,
   battery EVENTs, CRCs, footer accounting, queues and faults. Test simulated or
   controlled-source voltage cases under separate review before exhausting a cell.
4. Firmware finalisation stops acquisition, not electrical load. MCU/GPS continue
   drawing power; the protected pack remains necessary. Measure idle/record/charge
   currents and runtime. There is no claimed runtime from 300mAh alone.
5. Leave recorders stopped, disconnect the pack for storage using an appropriate
   safe procedure, and release the USB port. Retain all data; no automatic erase
   or retry follows any failure.

**Current decision:** GO for software review and planning a separately authorised
USB-only bench stage. NO-GO for LiPo connection/charging or battery-powered
recording until the boot/polarity/charger gates above are satisfied. The pack
specifications supplied by the user are recorded; they are not measurements of
this board or independent verification of the 1.2A charge figure.
