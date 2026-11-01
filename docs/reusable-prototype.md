# Reusable USB prototype and battery-monitor preparation

Switchless first-test preparation and exact existing commands:
[switchless-battery-first-test.md](switchless-battery-first-test.md). The ten-second
disconnect/finalise/reconnect workflow is verified in simulation, with no upload
or physical battery qualification. It uses this same corrected reusable firmware.

Latest stock-board-only assessment:
[stock-board-battery-startup.md](stock-board-battery-startup.md). It audits the
historical bootloader source and verifies the linked correction, with no hardware
or bootloader changes. USB-only testing is supported; battery connection remains
electrically unqualified. No external-monitor modification is required by that
software test plan.

Separate build: **tacticpod_reusable_usb**. No upload, COM-port access, battery
connection, physical recording, external flash program or erase was performed.
The installed 120-second engineering firmware and all three physical sessions
remain untouched. The normal TCB3-disabled environment remains the default.

**Startup safety follow-up:** the reusable project variant now preloads P0.14
LOW before OUTPUT in `initVariant()`. See
[battery-startup-safety.md](battery-startup-safety.md) for corrected build evidence
and the remaining **NO-GO for LiPo connection**. The artifact hashes and 25-suite
results below describe the preserved earlier review baseline.

## Recording behaviour

The prototype reuses TCB3 3.2.0 page/container integrity, the lossless block codec,
shared TIMER4 clock, sentence-scoped GPS records and TPX3 export. IMU acquisition,
GPS reception/configuration, sensitivities, V1 serializer, frozen V2 and decoder
formats are unchanged. No samples are interpolated, rounded or trimmed to force
a count. A time window can include an endpoint sample (the preceding physical
test had 12001 IMU readings); exact-count and time-window policies are different.

The first possible append is **0x039000**, address-derived **session 58**. Bytes
below it permanently protect the three current sessions in this prototype.
Later sessions start after the highest occupied header prefix, including torn or
unknown prefixes. Holes below occupied sectors are never reused. Missing prefix
history that would recover below the protection floor faults. A hidden nonblank
payload in the proposed candidate faults instead of searching past it.

The recording partition remains **0x000000-0xFFEFFF**, on the existing **128 Mbit /
16 MiB** chip. Reusable writes are restricted to **0x039000-0xFFEFFF**, with no erase
API. The diagnostic sector **0xFFF000-0xFFFFFF** is excluded. There is no chip erase,
status-register modification or automatic recording. V1 RECORD READ CSV remains
available; V1 recording mutations and all destructive diagnostics are blocked in
this build. Existing engineering environments retain their original limits.

Each START performs a read-only conservative scan and probes the complete initial
sector. Every 4096-byte sector is checked again before its header; every destination
page is checked before programming. The verified initial address is privately
snapshotted and rechecked by the store and physical adapter before the first WREN.
One 256-byte page program contains CRC32 and commit seal, followed by busy polling,
full readback and CRC/seal checks. Existing one-second program timeouts remain.

Sessions can repeat after clean finalisation and the read-only legacy append-cache
rescan. START while pending/active/exporting/faulted is refused. Flash failures
fault-lock immediately. Acquisition/queue failures stop capture, drain recoverable
data when possible, then lock further mutation; STATUS exposes `lock=1`. Incomplete
finalisation never automatically resumes. Reboot scans read-only; an explicit
new START can append after an interrupted occupied sector without overwriting it.
Only previously committed complete blocks are recoverable; RAM/in-flight data can
be lost during an actual power failure.

Default duration is **120 seconds**, configurable **1-1800 seconds** while idle.
Duration persists only in RAM until changed/reset. Capture stops without USB.
Finalisation has **60 seconds from the stop request**, including early/low-battery
stops, with a total automatic-stop upper bound of duration plus 60 seconds. The
watchdog runs even when foreground flash service is withheld. A stalled deadline
fault-locks rather than continuing indefinitely.

Before capture, admission reserves the worst-case sector budget for
`ceil(duration/2)+1` frames, each at the hard 8228-byte maximum, 216 frame bytes per
data page and 14 data pages per sector. Header/footer slots and padding are included
by reserving whole sectors. At 120 seconds this is 170 sectors / 696320 bytes; at
1800 seconds it is 2510 sectors / 10280960 bytes. There are initially 4038 sectors
above the protected prefix. Insufficient budget refuses START before programming,
increments the full counter and permits selecting a shorter duration. This is a
conservative admission estimate, not reserved space consumed after STOP: the next
session follows actual occupied sectors. Unexpected extra traffic can still stop
with explicit overflow/full accounting; it cannot bypass the fence.

## Commands and button plan

| Command | Behaviour |
| --- | --- |
| `TCB DURATION 120` | Set duration in whole seconds, idle only, 1-1800 |
| `TCB START` | Explicit append-only capture after scan, battery/capacity checks |
| `TCB STOP` | Stop capture and drain; does not erase |
| `TCB STATUS` | State/session/counters, duration, drain deadline and fault lock |
| `TCB READ` | Read-only raw TPX3 export of TCB3 sectors, all sessions |
| `RECORD STATUS` / `RECORD READ` | Existing legacy status/IMU-only CSV support |
| `BAT STATUS` | Source/trust/voltage/thresholds/calibration/invalid/timeout status |
| `BAT CAL 3972` | Idle-only one-point calibration against an actual meter reading |
| `BAT MODE CELL` | Idle-only explicit cell configuration; fresh warmup required |
| `BAT MODE USB` | Idle-only absent-cell mode; disables voltage-triggered stops |
| `BAT LIMITS 3600 3500 3300 3700` | Idle-only warning/stop/hard/recovery mV |
| Physical button | D0/P0.02, active LOW; hold 3s to start, tap then hold 3s to stop |

Battery settings, cell mode and calibration are **RAM-only**, never written to
recording flash or internal nonvolatile settings. Every boot defaults to cell
unconfirmed, uncalibrated and no recording. Calibration commands require a fresh,
plausible reading, accept a 2500-4300mV meter reference and bounded 0.8-1.2 gain.
They are not a substitute for a real reference measurement. USB mode must not be
used to pretend a connected cell has automatic protection.

BAT STATUS is allowed during recording, queued into the same bounded nonblocking
serial response buffer. Battery configuration is refused while busy. TPX3 export
suppresses all command text, including BAT STATUS; wait for export END before
sending another command. No periodic voltage printing or USB waiting is required.

The reusable build now uses the normally-open switch on D0/P0.02 with the
internal pull-up; source review found no other D0 ownership. Its debounced,
nonblocking state machine invokes the same `TCB START`/`TCB STOP` command path,
so storage, battery, duration and fault admission remain firmware-owned:

- From idle, hold continuously for 3 seconds to request one start. Releasing
  early is ignored. A busy scan, active session or fault cannot start a second
  session; a failed request never gets the recording-success indication.
- While recording, make a short tap (at most 500 ms), release, then press again
  within 5 seconds and hold for 3 seconds. A standalone long hold does not stop.
  Releasing the second hold early cancels shutdown. Finalisation remains
  nonblocking and the normal flash footer/verification must finish before the
  safe-stop indication.
- USB commands and the button share the TCB3 recorder state and reject duplicate
  operations. USB diagnostics/recovery remain available; reset still performs a
  read-only scan and never resumes or starts a session automatically.

The reusable build drives the active-LOW XIAO RGB LED using the variant's
`LED_RED`, `LED_GREEN` and `LED_BLUE` mappings. Ready/acquiring/no-fix uses a
100ms blue flash every 3 seconds; a current valid GPS fix alternates blue/green
on those same intervals. A trusted calibrated low-battery warning overrides
this with orange. Startup accepted is two brief blue flashes, capture actually
started is a 400ms blue flash, successful finalisation is two brief green
flashes, and a recorder fault repeats three red flashes. GPS fix freshness is
the same 5-second RMC/GGA validity used by GPS STATUS. LED updates do not delay
sensor/flash service.

## Standby, wake and USB recovery (System ON)

The button is **not a power switch**. After 2 seconds with nothing to do the
firmware enters a **System ON standby** (`src/standby_policy.h`). This is a
software idle state, not System OFF:

- GPS UART (`Serial1`) is stopped and its TX line parked HIGH; receiver
  configuration is untouched, so the module keeps running from its own supply.
- IMU accelerometer and gyroscope are written to power-down (`CTRL1_XL`/`CTRL2_G` = 0).
- The SPI NOR is put into deep power-down (opcode 0xB9) only when WIP/WEL are clear
  (`src/flash_power.h`); otherwise standby is refused. It is released (0xAB) at wake
  and always at boot, since a reset can occur while it sleeps.
- The 1 MHz TIMER4 clock is stopped; time continues from the RTC-based `millis()`.
- The user RGB LED is off. `loop()` only polls D0 (every 50 ms, 10 ms while pressed)
  using FreeRTOS `delay()` (tickless idle).
- D0 (pull-up), P0.14 (`VBAT_ENABLE`, LOW) and P0.13 (charge-current select) are never
  modified. The divider stays enabled; its current is unmeasured.

Standby is never entered while recording, starting, finalising, exporting, faulted,
GPS start-up/scan is running, a button sequence or LED pattern is active, USB VBUS is
present, or a serial port is open. Any of these exits standby. A pressed button
held for 3 seconds wakes the peripherals first, then requests a session.

**Why not System OFF:** wake from System OFF is a reset, so the time already held
cannot be measured and RAM (including the RAM-only battery calibration) is lost;
the Adafruit bootloader's behaviour on System OFF/USB wake, and the GPS/flash
breakout supply paths, are unverified on this board. System OFF is therefore **not
implemented** (source scan in `standby_tests.py`), and no sleep current is claimed.
It can be revisited after measuring the System ON standby current and bootloader.

**Hold timing:** the 3-second hold is measured from the debounced press (25 ms) as
seen by the 50 ms standby poll, so it is accurate to roughly +75 ms; this is a
design bound, not a measurement. Releasing earlier cancels the start and standby
resumes after the settle time. A button held at boot never starts recording.

**No automatic recording:** reset, bootloader exit, USB insertion and firmware
install never start a session; only a debounced 3-second hold does. The recorder
scan after reset is read-only.

**USB recovery:** with VBUS present or a serial port open the device does not stay
in standby, so serial commands (`TCB STATUS`, `TCB READ`, `BAT STATUS`) and the
bootloader double-tap reset work as before. If a port does not enumerate while the
device is in standby, press and release RESET (or double-tap for the bootloader)
and reconnect. Automatic USB wake is not claimed beyond this System ON behaviour,
which is untested on hardware. The bootloader is not modified.

## Recording until the button stops it

A physical-button start begins an **until-stop** session (no time cap). `TCB DURATION
UNLIMITED` selects the same for USB `TCB START`; `TCB DURATION 1..1800` still
selects the timed mode (`TCB DURATION 0` is refused). `TCB STATUS` reports
`mode=until_stop|timed` and `free_sectors=N`. The TCB3/TPX3 format is unchanged
(`src/tcb3_store.*` are byte-identical, hash-guarded in `standby_tests.py`).

Until-stop admission requires at least 8 free sectors. The store reserves page 15 of
every sector for the footer, so when the partition fills the session stops with
reason Full and is still finalised; this is treated as a clean completion (a readable
session, not a fault-lock). The next start is then refused ("insufficient session
capacity") with the red start-failure pattern. Existing sessions are never overwritten.
No maximum recording duration is claimed: it depends on measured compression and is
unverified. The 60-second finalisation watchdog still applies after a stop.

## LED summary

Ready/recording without fix: blue 100 ms every 3 s. Fresh fix: alternating blue/green.
Trusted calibrated low battery: orange, overriding GPS patterns. Start accepted: two
blue flashes; recording started: 400 ms blue; stopped: two green flashes; start refused:
three red flashes (2 s); fault: repeating red. Standby: off.

## Status and supervised hardware checklist

| Item | State |
| --- | --- |
| Firmware compiled | See the CI "Firmware build and tests" run on the PR (artifact + SHA256SUMS) |
| Host tests | See the same run; local runs documented in the PR |
| Hardware behaviour | **Not verified**: wake, hold, GPS/IMU timing, LEDs, USB, standby current |

Install (needs separate approval; never done by CI): download the artifact, verify
`SHA256SUMS.txt`, and install the `.hex`/DFU package through the existing bootloader.
Flashing is not modified here and recordings are not erased.

Supervised checks, USB-only first, each by the user: (1) connect USB, confirm
`TCB STATUS` and `BAT STATUS`; (2) disconnect, wait 3 s, tap D0 briefly: no recording,
no LED; (3) hold D0 3 s: two blue flashes then a blue confirmation; (4) confirm
GPS-fix alternation outdoors and 10 Hz/100 Hz sampling after download; (5) tap, release,
hold 3 s: green flashes and LED off; (6) reconnect USB and `TCB READ`, compare with the
TPX3 decoder; (7) measure standby current with a meter in the battery lead before any
claim; (8) battery connection stays NO-GO until the P0.14/P0.31 and charger items
above are qualified.

Do not begin a battery-powered outdoor trial until the documented boot
P0.14/P0.31 and charging-safety NO-GO is cleared. After those hardware and
power gates pass, the conservative first supervised capture recommendation is
120 seconds, followed by download and TPX3 verification before increasing the
duration. The firmware remains capped at 1800 seconds (30 minutes), not a
whole-match guarantee; admission checks storage and never overwrites old
sessions. Existing sessions remain protected by the append-only layout. This
recommendation is not field qualification.

See "Recording until the button stops it" above for the opt-in until-stop mode.

## Exact board support and hardware evidence

The installed board JSON is
`.pio-core/platforms/SeeedStudio/boards/seeed-xiao-afruitnrf52-nrf52840.json`.
It selects the non-Plus `Seeed_XIAO_nRF52840` variant; the existing build's Sense
flag selects the working IMU library path. Its battery pin mappings agree with
the installed Sense variant: Arduino **14 -> P0.14 READ_BAT**, **32 -> P0.31 AIN7**,
**22 -> P0.13 charge-current selection**. These are internal Arduino indices,
not header labels or interchangeable raw nRF pin numbers. No board ID was changed.

Seeed documents LOW on P0.14 for voltage sensing, warns about its HIGH state
during charging, and describes high-impedance P0.13 as nominal 50mA versus LOW
for 100mA. The implementation keeps the low-current selection and never drives
the charging-current pin HIGH/LOW or toggles the sense gate HIGH. The wiki's
prose and its older example differ; the installed core's input-mode default agrees
with the current prose. [Seeed board guidance](https://wiki.seeedstudio.com/XIAO_BLE/)

**Original vendor boot interval:** installed `variant.cpp::initVariant()` drives
VBAT_ENABLE HIGH before application setup. The new application asserts LOW before
other setup work, but cannot guarantee the bootloader/core interval. No vendor
core or bootloader was patched, and no pin-voltage measurement was made. This
conflict prompted the project-owned variant correction in the startup follow-up.
Reset/bootloader safety still requires qualification before LiPo connection.

The published schematic shows the nominal 1M/510k sensing divider and charger
temperature-sense configuration. It does not establish this individual board's
revision, resistor tolerances or cell temperature. [Seeed schematic](https://files.seeedstudio.com/wiki/XIAO-BLE/Seeed_Studio_XIAO_nRF52840_PDF.pdf)
The charger documentation identifies the non-H BQ25101 as a 4.2V single-cell
charger; charge/load behaviour and temperature supervision must still be verified
on the actual board. [TI BQ2510x datasheet](https://www.ti.com/lit/ds/symlink/bq25101.pdf)

No ADC reading, charge current, pack temperature, cell presence or runtime was
physically measured in this phase. A USB-powered charger node may produce an ADC
voltage even with no cell; voltage and charge indication cannot prove presence.

## Battery monitor implementation

`Battery::Monitor` is a portable fixed-memory policy. `BatteryRuntime` provides
the production ADC backend and an explicitly tagged host-only simulated backend.
There is no production USB command to inject a simulated voltage.

The real backend uses polled SAADC EasyDMA, one sample per second, 12 bits,
internal 0.6V reference with gain 1/6 and 40us acquisition. It uses no analogRead
busy waits, ADC interrupt or TIMER4. Configuration/read/completion/STOP are separate
foreground states, with offset calibration before first use and STOPPED handshake
before sampling after calibration. DMA storage is static/aligned. It refuses to
reconfigure an already enabled/interrupt-owned ADC; SAADC is otherwise reserved
to this backend. A 20ms wall timeout invalidates the reading without blocking.
The Nordic source-resistance table motivates the long acquisition setting; the
divider's nominal Thevenin resistance is about 338kOhm. Offset calibration alone
does not qualify the board divider or voltage accuracy. Temperature drift and
electrical accuracy need bench testing. [Nordic nRF52840 specification](https://docs-be.nordicsemi.com/bundle/nRF52840_PS_v1.8/raw/resource/enus/nRF52840_PS_v1.8.pdf)

Nominal conversion is `raw * 3600/4095 * 1510/510` mV, rounded to integer. Operator
gain calibration adjusts nominal values; a median-of-three followed by 1/4 IIR
filters valid readings. Raw rails, implausible 2000-4400mV range, backwards or
duplicate timestamps, ADC errors and stale readings are not trusted.
Three fresh valid samples are required after configuration/calibration. Readings
older than three seconds are unavailable. Source stays `ADC_UNVALIDATED` until
physical qualification; `trusted=1` means software policy eligibility, not a
factory accuracy guarantee. There is no inferred battery percentage/fuel gauge.

| Default policy | Value and behaviour |
| --- | --- |
| Warning | Filtered <=3600mV, latched through hysteresis |
| Safe-stop request | Filtered <=3500mV continuously for 3 seconds |
| Hard lower request | Instantaneous calibrated <=3300mV on two consecutive trusted samples |
| Recovery | Filtered >=3700mV for three samples clears low latches |
| New cell-mode START | Fresh/calibrated/warm, no stop latch, instantaneous and filtered above stop limit |

These are proposed prototype operating thresholds, not the cell's protection
trip values. Hard confirmation begins after warmup. Hysteresis/recovery never
automatically starts a session. Invalid or uncalibrated readings cannot generate
a new warning/stop; a previous genuine low latch remains until valid recovery.
Configured cell mode with unavailable calibration/voltage refuses a new START
as untrusted, rather than falsely declaring low voltage. USB-only mode permits
normal recording without a cell or trustworthy ADC reading.

Low/hard requests stop acquisition and finalise without USB. They **do not switch
off the MCU, GPS or cell**; idle load remains. Firmware is not a pack-protection
circuit, and sudden collapse can still lose RAM/in-flight data. The reported cell
protection is the independent electrical backstop, not a reason to run down to it.
No software charge termination, overvoltage/current disconnect or pack-temperature
protection is implemented; this monitor is not a replacement for the charger/PCM.

## Battery EVENT extension (no frozen-format change)

The frozen EVENT schema permits unknown nonzero codes. This separately documented
extension uses code **0x0100**, stream 0, count 1, severity 1 for warning or 2 for
stop, with the original u64 detection timestamp. Argument 1=warning, 2=filtered
low-stop request, 3=hard request. Detail bits 0-31 hold calibrated instantaneous
mV, bits 32-63 filtered mV. No sensor sample is substituted by the event.
An intentional battery stop has existing footer reason Clean when all observations
are safely retained; the EVENT explains why it stopped. If event enqueue/storage
fails, existing rejection/loss/fault accounting takes precedence. Existing decoders
retain the code/argument/detail without requiring a new container/schema version.

## Soshine cell and connection checklist

User-provided cell information: Soshine 602030, 300mAh, 4.2V charge voltage, 2.75V
discharge limit, reported overcharge/overdischarge/overcurrent/short-circuit
protection and reported 1.2A maximum charge current. The exact manufacturer's
datasheet was not located; the 1.2A figure is **not independently confirmed**.
Do not borrow specifications from other brands' 602030 packs. Firmware leaves
nominal low charging current, about 50mA (about 0.17C for 300mAh), subject to
measurement. No temperature range, connector pin order or actual protection trip
voltage is inferred. See [battery-bench-checklist.md](battery-bench-checklist.md).

## Validation, resources and decision

All 20 prior suites plus policy, real-backend register model, reusable store,
reusable console and reusable integration suites pass. The actual three-session
233472-byte image is a deterministic fixture, with provenance and hash. Simulations
preserve that entire prefix and independently decode/export all old and new
sessions. Three successive USB-disconnected sessions produce new IDs 58,60,62;
the six-session example is 282304 TPX3 bytes and 481056 exact canonical bytes.

Policy tests cover absent/USB-only, uncalibrated, stale/invalid/duplicate readings,
filter outliers, warning hysteresis, low dwell, hard threshold, start admission
and recovery. Register tests exercise actual production ADC sequencing, ownership,
timeouts, VBUS detection and calibration diagnostics with simulated registers.
Integration tests demonstrate low-stop at about 15.74s and hard-stop at about
7.74s for the chosen simulated drain, with clean battery EVENTs and no lost
observations, no USB, and no automatic restart on voltage recovery. They also
cover dynamic durations, repeated START, acquisition fault-lock, timeout,
insufficient storage, hidden payload, interruption/recovery, clock rollover,
USB battery diagnostics during capture and binary export isolation.

A direct store boundary test writes only the last recording sector
0xFFE000-0xFFEFFF, finalises with explicit full/loss accounting, programs each
page once, and leaves 0xFFF000-0xFFFFFF unchanged. It bypasses frontend capacity
admission solely to exercise the store boundary in simulated NOR. Existing suites
continue high-entropy/raw-fallback and whole-capacity throughput checks.

Simulation maxima include 544us store-service wire model, around 358ms block
persistence, input/output high-water 2/1 and no normal-run losses. These are not
new hardware WCET measurements. Static RAM/program figures, artifact hashes and
all exact commands/results are in `logs/reusable-review/`; live stack/heap and
power consumption are not measured. The default disabled ELF remains byte-identical
(`a83cbaabf9c0e18034a5f70a987539c77c574e429e9b6aa3a06caf6d1a836089`).

**GO for review and a separately authorised USB-only upload/read-only bench stage.**
**NO-GO for connecting/charging the LiPo now**: qualify the boot sense-pin interval,
actual pad/connector polarity and charger behaviour, and confirm the cell's usable
charge specification first. No battery-powered recording/deployment is qualified.
There has been no new physical test, port access, upload or battery connection.

## Reviewed build and reproduction

| Build | Static RAM | Program storage |
| --- | ---: | ---: |
| Default, TCB3 disabled | 118992 bytes | 165336 bytes |
| `tacticpod_reusable_usb` | **120836 bytes** | **189424 bytes** |

The prototype adds 144 static RAM bytes compared with the preceding engineering
build. Available regions are 237568 RAM bytes and 811008 program bytes. No heap
allocation is added. The real ADC backend is compiled into this ZIP; the injected
backend exists only in host tests and is tagged SIM in diagnostics.

Archive: `logs/reusable-review/tacticpod_reusable_usb.zip`.

| Artifact | SHA-256 |
| --- | --- |
| ZIP | `a0a6d7399342cae4fa351027a190d043e6c38d1e6dd3ee6e8c738e978ba3f680` |
| ELF | `90d779d3468b4b337ab95f0a252cb9e676eaeac39bd975a4c2a3182cd63e0102` |
| Application payload | `a965a052e40b05f95d3ad7eb7472fa45fb630d51e80eb48dc9796302c1d460bf` |

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e tacticpod_reusable_usb
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/reusable/run_all_tests.py
```

The unified runner executes all 25 host regression suites and writes new logs
under `.pio/reusable-regression-logs/`. It opens no serial device. The reviewed
run's exact commands and exit codes are in `logs/reusable-review/regressions.json`.
Build commands/exit codes, source hashes, BSP hashes and artifact hashes are also
archived there. The example TPX3 and decoder output support future software review;
they contain three real archived sessions plus three explicitly simulated sessions,
not newly recorded physical data.

New production files: `src/battery_monitor.h`, `src/battery_runtime.h`,
`src/battery_runtime.cpp`. Updated production files: `platformio.ini`,
`src/tcb3_test_limits.h`, `src/tcb3_store.h`, `src/tcb3_store.cpp`,
`src/tcb3_session.cpp`, `src/main.cpp` (early conditional battery initialisation),
`src/serial_console.cpp`, `src/recorder.cpp`, `src/flash_write_test.cpp`.
New tests/fixture provenance are under `tests/reusable/`; `tests/recording/SPI.h`
adds the existing SR2 mock response to the reusable profile. No vendor core,
bootloader, active IMU-read loop, GPS receiver, compressor or decoder source was
modified. Frozen V2 and V1 serialization hashes remain unchanged.
