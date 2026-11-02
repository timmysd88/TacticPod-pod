# First physical-trial fixes

This application update addresses the first button/LED/GPS trial. It has not
been uploaded or tested on hardware. The TCB3 store and TPX3 format are unchanged.

## Evidence and root causes

The preserved trial archive (`logs/phase5-button-2026-11-02/after-test.tpx3`,
SHA-256 `ddb6dec14503ccdeca5c81259cdad39fabc9331af1f8788a703aa3c37c5f1a91`)
contains five new, clean sessions, rather than the owner's two described attempts.
All five earlier sessions are unchanged. The old firmware had no timestamped
button events, so individual gestures cannot be reconstructed from these footers.

The first sustained approximately 1 Hz RMC interval begins 36.882 s after session
74 starts, following a 401.934 ms gap. Earlier intervals are approximately 100 ms.
Later sessions remain approximately 1 Hz. Subsequent actual receiver RAM readback
shows MEAS=1000 ms and extra NMEA outputs, despite the cached profile-success text.
This locates the observed cadence change **inside an active session**, rather
than solely at its later standby transition; it does not establish the exact
instant or physical cause of receiver configuration loss.

Source inspection: standby ends the MCU UART, parks MCU TX HIGH, powers down the
IMU and NOR, and suspends TIMER4. Wake restarts UART at 38400. There is no GNSS
reset command, receiver supply switch or intentional persistent-settings write
in this path. The owner reports no XIAO reset/battery disconnection. A receiver
reset/power disturbance cannot be confirmed or excluded from the available data.
The firmware previously neither invalidated nor rechecked its cached success on
wake. The new RAM readbacks and loss events make this observable on the retest.

The stop LED defect is confirmed in the main-loop lifecycle: recording activity
ends before the asynchronously committed footer produces its completion event.
The old previous-recording flag was already false when that event arrived.
Separately, baseline blue/green pulses ran whenever awake, including idle.

## Application changes

- A completion-pending latch survives finalisation; green flashes occur exactly
  once after the clean footer event. Faults clear the latch and retain red fault
  indication. No green success is emitted for failed finalisation.
- Idle/standby LEDs are off. Recording alone enables 100 ms pulses every 3 s:
  blue without a fresh fix, alternating blue/green with one, trusted battery
  warning orange overriding GPS colours. Start and clean-stop confirmations are
  separate one-shot patterns.
- Each reusable start waits for a fresh, complete receiver RAM audit, including
  UART/protocol/power guards. Capture/flash allocation do not begin until it
  matches. Loss invokes the established SPG 5.10 RAM-only transaction, ACK,
  readback and rollback engine; an unverified result refuses recording. Preparing
  can take several seconds; do not interpret start acceptance as capture begun.
- UART standby invalidates the observed profile. Wake requests a new audit and
  recovery if necessary. No MCU UART/baud, GPS OTP, BBR or flash layer changes.
  Three consecutive slow RMC intervals invalidate the current profile during
  capture and log loss; configuration writes are deferred until capture stops.
  Existing capture then continues with explicitly unverified GPS, rather than
  hiding missing GPS messages or changing recording data. Idle off-USB also
  audits periodically while awake; USB diagnostics do not trigger periodic
  reconfiguration merely because a snapshot has aged.
- GPS STATUS distinguishes the historical last-applied profile from current
  readback state and its t_ms. Snapshots older than 15 s are labelled stale, not
  continuously verified. GPS CONFIG mismatches invalidate current status.
  Configuration verification is never claimed as proof of valid navigation.
- FIELD EVENTS drains a 64-entry foreground-only RAM ring while stopped. Events
  include debounced press/release, start acceptance, session creation, stop arm/
  acceptance, finalising, clean completion/fault, GPS verified/lost/failed, wake
  and standby entry. Each entry has wrapping 32-bit boot millis, session/detail
  and overwrite count. Oldest entries overwrite explicitly; reset loses RAM.
  No allocation, flash writes, interrupts or serial output occur when recording
  an event. Output is one bounded line per idle slice, respects USB backpressure,
  and is suppressed during capture, binary export and GPS diagnostics.

The button state machine is retained: 25 ms debounce, 3 s continuous start,
<=500 ms tap then release and second 3 s hold initiated within 5 s to stop.
One press cannot start twice, a held-at-boot button requires release, and a lone
long hold during recording cannot stop it. The trial's five starts remain
unexplained until timestamped physical evidence is collected.

## Validation and artifacts

Use the pinned PlatformIO environment `tacticpod_reusable_usb`. The release bundle
is preserved locally under `logs/field-fix-review-2026-11-02/` with firmware.bin,
application DFU ZIP, HEX, ELF, build/test logs and SHA256SUMS.txt. Its validation
summary records the final results. No bootloader, framework, charging setting,
battery GPIO, calibration, D0 wiring or storage-format change is part of this PR.

The complete regression runner includes production main-loop integration cases
for delayed footer/green ordering, failure without success indication, bounce,
held start/stop, USB wake, receiver reset in standby, awake profile loss/recovery,
commit rejection/rollback, missing receiver, held-at-boot and bounded telemetry
under backpressure. Existing recorder tests use an independent UBX transaction
model to satisfy the new readiness gate. Portable C++ tests use host g++ where
available, otherwise pinned ARM GCC and Unicorn; no toolchain upgrade is needed.

## Release notes

Application environment: `tacticpod_reusable_usb`, Seeed XIAO nRF52840 Sense,
pinned PlatformIO 6.2.0 / Seeed a6fd41d / Arduino 1.1.1 / GCC ARM 7.2.1.
Clean application build: flash 202312/811008 bytes, RAM 122096/237568 bytes.
The unchanged bootloader and SoftDevice are not included in the application ZIP.

| Artifact | SHA-256 |
| --- | --- |
| firmware.bin | `77829105dd9943da9a13634d271ae7b6e381354b91cba788f6a945f9a90f00da` |
| firmware.hex | `c522e89ffa5fe32308d94aee960f7743e58a7cd8d296665905209f8350f8e231` |
| firmware.zip | `fd93c753131901e3500ede98e2f388fa3fed4e338bc75542b5c9efba0a70b787` |

The binary is extracted directly from that application-only ZIP. No on-device
result is claimed for this release. Upload and recording need separate approval.

## Instrumented hardware retest — separate approval required

1. Obtain separate application-upload approval and then recording approval. Keep
   the factory bootloader and all recordings. First download/verify a fresh TPX3
   baseline; the last verified device inventory is ten complete sessions through
   122 and next append 0x084000 (next ID 133 only if that baseline is unchanged).
   Check wiring, polarity, measured cell voltage, temperature and antenna before
   testing. No calibration/charging changes are included.
2. With the approved application on USB, wait for startup recovery to finish.
   Check TCB/RECORD/COMP/OBS/BAT status; require idle, empty queues, unlocked and
   zero faults. GPS CONFIG must read MEAS=100, NAV=1, RMC=1, GGA=10,
   GPS/Galileo on, BeiDou off and UART=38400; unwanted NMEA divisors zero.
   GPS RATE must measure approximately 10 Hz RMC and 1 Hz GGA. Fix/UTC validity
   is a separate assessment. Save FIELD EVENTS output to establish a baseline.
3. After recording approval, disconnect USB, wait 10 s with the button released;
   LED must settle off. Record a video/timer showing every gesture and colour.
   Hold D0 once for 3 s, release after the two blue acceptance flashes, then wait
   for the longer blue **capture-start** confirmation. Do not repeat the start
   while the GPS audit runs. Red or missing confirmation: reconnect and diagnose.
4. Time approximately 30 s from capture confirmation. At about 27 s tap for
   0.1–0.3 s, release for about 0.2 s, then hold for at least 3 s. Release after
   the two green completion flashes. Keep battery power through finalisation.
   Expect LED off after confirmation/settle; do not retry if ambiguous.
5. Reconnect USB without resetting; wait for wake audit, then save statuses and
   FIELD EVENTS. Require overwritten=0, exactly one start_accepted, one
   session_created with the new ID, one stop_armed/stop_accepted, one finalising
   and one finalised_clean in order, plus return-to-standby/wake evidence. Retain
   all lines and note GPIO/video timings. A reset destroys this RAM evidence.
6. Download/decode TPX3 to a new filename. Require exactly one new complete clean
   session, every old session and recorded prefix byte-identical, valid packet/
   page/block CRCs, no loss/missed/rejected/full/incomplete counters. Report actual
   timestamp duration, IMU rate/gaps, RMC/GGA counts and fix/UTC validity; use actual
   manual duration rather than requiring exactly 30.000 s. Rough targets for
   30 s: 3000 IMU / 300 RMC / 30 GGA. Earlier IMU measured about 99.4 Hz; do not
   report exact 100 Hz without new evidence. No further start, erase or reset to
   hide a discrepancy. Standby current and thermal behaviour remain measurements
   for the owner; no electrical qualification is inferred from these tests.
