# Phase 2: shared timing and RAM observations

Implemented against the frozen [V2.0 format](recorder-v2-format.md). This phase
adds no V2 flash writer, partition change, new recording format, or export command.
The existing V1 writer and RECORD READ implementation are unchanged. No firmware
upload or device flash access was performed during development.

## Clock and acquisition

`MonotonicClock` owns TIMER4 exclusively at 1MHz, 32-bit TIMER mode. Compare
channel 0 is set to zero to detect each 2^32-microsecond revolution; its ISR
extends an upper word even when no observation is requested. Channel 1 captures
the lower word. The reader briefly masks interrupts, captures the counter and
services a pending wrap before returning a u64. It restores the original IRQ
mask, does not wait in a loop, and cannot double-count a pending ISR. Peripheral
event clear is read back before ISR return. A revolution is about 71.58 minutes.

The clock starts once at the beginning of setup, before GPS/IMU initialisation.
It includes the BSP millisecond boot offset sampled at initialisation; that one
initial offset is millisecond-quantised, subsequent timer intervals resolve to
microseconds. `begin()` is idempotent. OBS START/STOP/CLEAR and RECORD commands
never reset it. Reboot starts a new domain; boot/session identity generation is
still a future phase. TIMER4 is not otherwise used by the current application;
the installed BSP has nrfx TIMER drivers disabled. Enabling another TIMER4 user
later requires resolving ownership explicitly.

This deliberately avoids the installed BSP `micros()` implementation in
`cores/nRF5/delay.h`: that function selects either coarse RTOS ticks or the DWT
cycle counter divided by 64, with different rollover properties. Hardware
register configuration follows the [nRF52840 product specification](https://docs-be.nordicsemi.com/bundle/nRF52840_PS_v1.2/raw/resource/enus/nRF52840_PS_v1.2.pdf?save_local=true).

TIMER4 needs the high-frequency clock, so power consumption must be measured on
hardware. Interrupts must not be masked for an entire timer revolution. Neither
this code nor the existing firmware does so; debugger halts, pathological
interrupt masking and System OFF are not guaranteed clock-continuity cases.

Every actual IMU read captures the shared timestamp immediately before its first
axis read. The original V1 `millis()` timestamp and CSV semantics remain untouched.
IMU reads remain 100Hz while V1 recording; normal idle acquisition remains 10Hz.
OBS START can independently request 100Hz RAM acquisition while V1 stays stopped.
No replacement samples, interpolation or scheduler/ODR changes were introduced.

These are software observation timestamps, not simultaneous register samples or
GPS measurement-time/PPS timestamps. Existing SPI, I2C, USB and scheduler latency
still need measurement; deterministic simulated spacing is not a hardware jitter
guarantee.

## Sentence-scoped capture

The existing bounded Serial1 reader remains the sole UART consumer. After UBX
demultiplexing, each non-UBX byte also reaches `RamObservations::observe()`.
An intervening UBX frame cancels a partial NMEA observation. GPS configuration,
TinyGPSPlus, GPS RATE and existing diagnostics retain their original paths.

The observer uses one fixed 192-byte frame buffer, requires complete CRLF and
valid checksum, timestamps completion, and parses fields into a freshly zeroed
record. It supports uppercase two-letter talker IDs, not just GP/GN. Partial
frames expire after one second; overlong/control-corrupted frames are abandoned
and resynchronised at the next `$`. Checksum-invalid frames do not become GPS
observations. Checksum-valid recognised sentences with malformed/missing fields
remain observations with explicit absent-field bits. Nonempty invalid fields
increment a diagnostic counter. No TinyGPSPlus cached values are read.

RAM records are the exact frozen little-endian byte arrays: IMU_RAW 24 bytes,
GPS_RMC 44, GPS_GGA_QUALITY 40, EVENT 32. No compiler struct padding is stored.
Integer conversions check limits before setting presence bits; no clipping or
wrapping. Decimal measurements round to the nearest stored unit, half away from
zero. RMC knots convert to mm/s using the exact 1852m nautical-mile definition
and a micro-knot intermediate. UTC fractions are truncated to milliseconds,
with up to nine fractional digits accepted. Date mapping is 2000–2099.

RMC preserves coordinates/speed/course from that sentence even on V status, but
the usable-fix bit is clear. A status A observation with mode N is also unusable.
Zero coordinates/speed are valid when present. Missing date/time/speed/course
cannot inherit an earlier value. Duplicate and backwards UTC observations remain
separate records, and their timestamps do not assert a valid navigation cadence.

GGA quality stays separate; every received GGA is captured, not artificially
resampled. The existing GPS profile requests 1Hz. Altitude/geoid fields require
metre units. HDOP remains dimensionless. Missing quality is 255. A derived date
is added only from the most recent RMC containing both parsed date/time, with
device age <=1.5s and UTC difference within +/-1.5s, including adjacent-day/year
rollover. A new RMC without date/time invalidates the association anchor. No
retrospective mutation of already queued GGA records occurs.

Unsupported `ss=60` UTC clears time presence and creates the specified bounded
UTC-discontinuity EVENT (code 5, argument 2). The event carries source stream and
prior UTC when available. Other event types/receiver silence detection are not
implemented in this RAM-only phase; silence simply produces no new GPS records.
No accuracy values or inferred metre errors are invented.

## Bounded queues and commands

| Stream | Capacity | Record bytes | Queue payload RAM |
|---|---:|---:|---:|
| IMU | 256 | 24 | 6144 bytes |
| RMC | 32 | 44 | 1408 bytes |
| GGA | 8 | 40 | 320 bytes |
| EVENT | 16 | 32 | 512 bytes |
| Total | | | **8384 bytes** |

Separate queue bookkeeping, counters, parser and diagnostic snapshots add further
RAM. All allocations are static. `pop()` returns a copy, never a mutable reference
to an entry. Queued observations are immutable; future code can merge the streams
by their shared timestamp. No automatic queue-to-flash consumer exists yet.

RAM capture defaults ON at boot at the existing acquisition rate. Queues retain
their earliest pending observations and reject new entries when full; there is
no overwrite. Observed/accepted/dropped counts and timing continue updating even
when entries are rejected. This diagnostic retention policy deliberately does
not stop V1 recording. A future V2 writer must implement the separately specified
recording failure/loss policy; these RAM drops must not be confused with V1
`dropped_samples`. Clear before a focused measurement to exclude boot/configuration
transitions. At 100Hz, an undrained IMU queue fills after 2.56s; RMC fills after
3.2s at 10Hz. This phase is not a long-duration RAM logger.

| Command | Behaviour |
|---|---|
| OBS STATUS | Snapshot per-stream counts, queue usage/high-water and min/max device timestamp spacing, plus parser errors |
| OBS START | Clear RAM queues/statistics, enable capture, request 100Hz IMU even when V1 recording is stopped |
| OBS STOP | Disable RAM capture and its independent 100Hz request; does not stop/change V1 recording |
| OBS CLEAR | Clear RAM queues/statistics and sentence/date-association state; preserve capture/rate request and boot clock |

Reports are bounded complete lines, emitted only when USB has room. No per-sample
USB output or busy wait is introduced. Slow/disconnected USB cannot stop capture.
Reports are snapshots at command time; timing fields are zero before two samples
have been observed. API statistics additionally expose first/last timestamps.
Commands during CSV export are refused without changing capture/queues, and their
refusal report waits until export ends, preserving the CSV stream. These commands
do not send any GPS configuration or invoke flash commands.

## Regression evidence

`tests/gps/observation_cases.h` runs production clock/parser/queue/main-loop code
against ARM UART, TIMER4, IMU and NOR models. Register tests cover pending overflow,
already serviced ISR, capture-at-wrap races, multiple revolutions without polling,
IRQ-mask preservation and idempotent initialisation. The mock adds configurable
IMU read duration to verify the timestamp precedes all six axis reads.

The GPS runner passes emitted ARM record bytes directly to the frozen Phase 1
Python `decode_record()` for IMU, RMC, GGA and leap-second EVENT conformance.
Other cases exercise fresh invalid masks, malformed fields, checksum rejection,
frame bounds/timeouts, UBX interleaving, missing UTC, no-fix/mode N, GGA date
association, queue overflow/copy isolation, USB backpressure, CSV isolation and
OBS STOP independence from V1 recording.

In deterministic 15-second simulations, 1500 IMU observations have min/max spacing
10000us, 150 RMC observations have min/max spacing 100000us, and 15 GGA observations
are captured. V1 concurrently commits all 1500 samples without loss or missed
deadlines. A second test leaves all RAM queues undrained: drops are 1244 IMU,
118 RMC and 7 GGA, while V1 still commits all 1500 samples without loss. GPS-silence
simulation retains 1500 IMU observations and creates no fake GPS samples.

The mocks do not model real I2C/SPI wire duration, oscillator accuracy, NVIC timing,
USB scheduling or electrical UART/flash failures. Hardware resource/power/timing
validation is required after a separately instructed upload. No V2 data has been
written to physical flash.

Final Phase 2 validation: 255 tests pass (190 GPS/clock/observation, 20 V1
recording, 13 flash diagnostics, 32 host format tests). The 30 new ARM scenarios
are included in the GPS total. PlatformIO build succeeds without warnings:
static RAM 80,456 / 237,568 bytes (33.9%), an increase of 9,700 bytes from Phase 1;
program flash 142,352 / 811,008 bytes (17.6%). Queue payloads account for 8,384
bytes of the RAM increase; bookkeeping, parser, clock and report state use the rest.
Hashes confirm `recorder.cpp`, `recorder.h`, `recorder_format.h`, existing flash
diagnostics, GPS configuration/profile/rate code and `platformio.ini` are unchanged.

Run all suites plus the normal build:

```powershell
python tests/gps/run_tests.py
python tests/recording/run_tests.py
python tests/flash_write/run_tests.py
python tests/recorder_v2/run_tests.py
platformio run
```
