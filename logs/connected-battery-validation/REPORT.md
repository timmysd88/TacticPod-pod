# First connected-battery read-only validation — 1 November 2026

**Read-only checks passed. No firmware problem identified requiring another build.**
The USB-connected system is responding normally within the diagnostics exercised.
Battery-supplied operation, charging safety and source handover are not established
by these results. The 300mAh battery connection is user-reported physical context.

USB access and read-only downloads were explicitly approved in chat. Application
COM8, VID:PID2886:8044, serial0E8A89B7289430BD, was opened at115200 and released.
No upload/build, bootloader action, reset, recording START/STOP, flash program/erase,
battery configuration, charge-control GPIO change or manual GPS configuration was
performed. Host evidence files and a proposed procedure were added only.

## Current firmware and recorder

Installed application is consistent with the previously uploaded
`tacticpod_reusable_usb`, TCB3 **3.2.0**: runtime reusable marker and duration/drain/
lock interface match; all four exported containers report3.2.0. The previous upload
archive SHA256 is `ffee03178a49e2b77130da9afdefd5f5c358c6b22ecb3e1e249b2157ca540c67`.
This visit did not cryptographically read back internal application firmware.

| Check | Current result |
| --- | --- |
| TCB | state1/Idle, session0, lock0, errornone |
| TCB duration / drain | 120s /60s; unchanged |
| TCB rejected / missed / full / CRC / incomplete | 0 /0 /0 /0 /0 |
| TCB programs / blocks / committed / offered | 0 /0 /0 /0 in current runtime |
| Legacy recorder | STOPPED, no buffered/dropped/missed samples |
| Next append | **0x03C000**, expected next address-derived session61 |
| OBS | captureOFF, RAM_fast_imuOFF, all queues/overflows zero |
| COMP | capturing0/fault0, input0/2/output0/2, rejected0 |
| Flash identity | JEDEC0B4018 five times; SR1 before/after00, SR2=02 |
| SFDP | Both256-byte passes match each other and archived hardware fingerprint |
| Established flash density | 16,777,216 bytes /16MiB, from matching SFDP fingerprint |
| IMU live check | Not exercised; IMU printing remained OFF; no present-rate claim |

TCB session0 and zero runtime programs do **not** mean recordings were lost.
This runtime has no newly started TCB session; the persistent inventory is separate.
The default duration120 and cleared RAM session/counters are consistent with an
application restart since the prior test, whose runtime retained session58/duration10.
They do not identify when/why that restart occurred or diagnose a battery fault.
Runtime counters are not lifetime fault counters.

## Battery evidence and interface limits

First BAT response: nominal3977mV/filtered3977mV. Final response:

```text
BAT source=ADC_UNVALIDATED cell=0 calibrated=0 valid=1 trusted=0 nominal_mV=3969 filtered_mV=3979 warning=0 stop=0 hard=0 adc_timeout=0 invalid=0 usb_vbus=1 limits=3600/3500/3300/3700 gain_ppm=1000000
```

**Voltage is uncalibrated and not trustworthy as an accurate cell-voltage value.**
The observed nominal range was3969–3977mV; filtered3977–3979mV. `valid=1` means a
fresh plausible ADC result, not bench-verified accuracy. `calibrated=0`, unity gain
and `ADC_UNVALIDATED` disclose the actual state. ADC offset calibration in the
backend is distinct from meter-based battery/divider calibration. Even software
`trusted=1` would mean policy eligibility, not certified electrical accuracy.

Source inspection: 12-bit SAADC, internal0.6V reference/gain1/6,40us acquisition,
nominal1M/510k divider, nominal conversion `raw*3600/4095*1510/510`mV,
one sample/second, median3 plus quarter-IIR filtering, three-second freshness.
No timeout/invalid samples were reported in this runtime.

`usb_vbus=1` is actual nRF VBUS detection, independent of the monitor being open.
It establishes USB voltage presence, **not** exclusive USB supply, charging status
or the battery's contribution to load. `cell=0` is unarmed battery policy; it is
not physical absence detection. The banner's “battery absent/unarmed” is static
wording, not a sensor finding. The supplied physical context says a cell is fitted.

Warning/stop/hard all zero provide no battery-safety assurance: this unarmed,
uncalibrated policy cannot trigger voltage-based warning/stop. Existing limits
remain3600/3500/3300/3700mV, unchanged. Calibration/mode/limits are RAM-only.
No reliable state-of-charge percentage, remaining runtime, cell-presence detection,
charge state/current, pack temperature, actual charging GPIO readback, power-path
current direction, minimum voltage history or persistent reset count is exposed.
Filtered voltage is adjusted internally; instantaneous calibrated voltage/raw ADC
and sample timestamp/age are not separately printed by BAT STATUS.

Reviewed local battery documents: reusable-prototype.md,
switchless-battery-first-test.md, battery-bench-checklist.md and
stock-board-battery-startup.md. The corrected variant clears P0.14 LOW before
OUTPUT; earlier bootloader/core/reset electrical exposure remains unqualified.
Application leaves P0.13 input/no pulls for documented nominal low charging current;
this is source evidence, not measured charger current or proof of safe charging.
No electrical safety conclusion follows from these serial values.

## GPS

Current profile: verified RAM10Hz, MEAS100ms/NAV1, GPS+Galileo, UART38400,
RMC every epoch/GGA every tenth epoch. NMEA fresh, cumulative checksum_fail0.
Receive-only15-second RATE observed150RMC and15GGA, zero checksum failures,
overlong frames, duplicate/backward/missing UTC epochs. Thus output cadence is
normal at10Hz/1Hz. **All fixes invalid/no fix**, satellites0, HDOP99.99,
position/speed unavailable. This establishes live receiver communications and
advancing UTC, not valid navigation solutions; cause of no fix is undetermined.
GPS reception/cadence can support a raw logging trial; valid movement/location
validation needs a fresh fix before START.

## Four-session preservation and duplicate export

Two new downloads `baseline-1.tpx3` and `baseline-2.tpx3`, each **268864bytes**,962
packets, passed packet CRC/sequence validation and were decoded separately.
Both files are byte-identical to each other **and** the previous verified
`logs/switchless-usb-hardware/after-1.tpx3` archive. SHA256 for all:

`92708b9d73289781ba296fde11afe6498d433dc34aa2f3ff75ebae95f28ff6ac`

| Session | Allocation | IMU / RMC / GGA | Status |
| --- | --- | --- | --- |
| 1 | 0x000000–0x001FFF | 500 /50 /5 | Complete, unchanged |
| 3 | 0x002000–0x00AFFF | 3000 /300 /30 | Complete, unchanged |
| 12 | 0x00B000–0x038FFF | 12001 /1200 /120 | Complete, unchanged |
| 58 | 0x039000–0x03BFFF | 1000 /100 /10 | Complete, unchanged |

Both full decodes had no root/session issues. Every session was also decoded in
isolation from its allocated sectors for **each** download, matching its full
decode's canonical raw data. Page CRC/commit seals, block CRC/continuity and footer
accounting checks passed. Every footer has clean reason0 and zero losses,
rejected/missed/full/CRC/incomplete counters. Existing committed bytes are preserved.
This export covers the TCB recording image, not a complete diagnostic-sector dump.

## Exact commands used

Host enumeration, which opens no serial port:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m serial.tools.list_ports -v
```

After permission, the one read-only host run:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" logs/connected-battery-validation/read_only.py
```

Exact serial command sequence (newline terminated):

```text
BAT STATUS
TCB STATUS
RECORD STATUS
OBS STATUS
COMP STATUS
GPS STATUS
DIAG ID
GPS RATE
BAT STATUS
GPS STATUS
TCB READ
TCB READ
TCB STATUS
BAT STATUS
```

Other existing diagnostic commands identified but not issued: `DIAG` (summary
JEDEC/SFDP), `GPS RAW` (receive capture), `GPS CONFIG` (UBX configuration polls),
`GPS SCAN` (temporarily scans host UART baud), `IMU ON/OFF` (live preview controls),
`RECORD READ` (legacy CSV). No separate POWER/charging diagnostic command exists.
`GPS 10HZ TEST`, `GPS RESTORE`, BAT CAL/MODE/LIMITS are configuration controls,
not read-only status commands, and were not used. Full firmware command discovery
is in src/serial_console.cpp; battery formatting/backend in src/battery_runtime.cpp;
policy in src/battery_monitor.h; recording timing/guards in src/tcb3_session.cpp.

## GO/NO-GO and prepared test

**Software GO:** existing firmware supports the proposed30s capture, autonomous
stop and60s finalisation deadline without USB; flash access and preservation pass.
No genuine firmware fault was found, so no new firmware build is warranted.

**Overall NO-GO pending physical battery/charging qualification or explicit review
of those unresolved limits.** The prior electrical uncertainties remain unresolved
by this read-only visit: actual cell/charger/current/thermal behaviour, startup
sense-pin exposure and physical source handover. Battery operation is unverified,
not diagnosed defective. A missing GPS fix separately prevents valid navigation
validation. Neither this report nor a later recording pass certifies battery safety.

The [proposed30-second procedure](../../docs/connected-battery-30s-test.md) is ready
for separate approval: USB start, unplug after STARTED, timed battery finalisation,
reconnect after100s, duplicate independent exports, preservation against this
four-session baseline. **It has not been initiated.** No recording permission was
requested or inferred from the read-only serial permission.

## Fields TacticPod Lab should display

| Group | Display fields |
| --- | --- |
| Device/firmware | Port, VID/PID/serial; connection state; application/profile evidence and verification method; container version; last successful poll |
| Power/battery | VBUS present; source contribution **unknown**; battery physically fitted **user-confirmed**, distinct from policy cell flag; source tag; nominal/filtered mV; valid/calibrated/trusted separately; gain_ppm; warning/stop/hard; thresholds; ADC timeout/invalid counts |
| Battery limitations | Low-voltage policy armed/disabled; voltage accuracy unqualified; charge state/current/temperature/SOC/runtime **unavailable**; no fabricated percentage or “safe charging” badge |
| Recorder | TCB state, current runtime session distinct from latest stored session, duration/drain, lock/error, next append/expected next ID, blocks, offered/committed/programs, rejected/missed/full/CRC/incomplete, max_service_us/max_block_ms |
| Other acquisition | RECORD state/buffer/drop/deadlines; OBS capture/fast-IMU/queues/overflows/parser counters; COMP capture/fault/queues/high-water/rejection/backpressure/raw fallback/slice timing; IMU preview state if known |
| GPS | Profile/UART; NMEA freshness/age; checksum counters; fix validity/satellites/HDOP; position/speed/UTC with validity/age; separate RMC/GGA cadence and unique/duplicate/missing/backward UTC counts |
| Flash | JEDEC, SR1/SR2, SFDP density/repeat/fingerprint result and unresolved identity qualification; protected boundary |
| Stored data/export | Inventory IDs/addresses/container/status/counts/footer/loss; export bytes/packets/hash; each integrity result; duplicate match; baseline preservation; interrupted/partial download status |
| Test evidence | Authorisation state; STARTED/disconnect/reconnect times; autonomous footer result; expected versus measured duration/counts; resets unknown unless evidence exists |

Future firmware fields that would require separate development: raw ADC,
instantaneous adjustedmV, sample age/timestamp, boot/reset reason and persistent
boot counter, explicit IMU health/sample continuity. Charger current/temperature
and power-path contribution need appropriate hardware measurements; Lab cannot
manufacture them from existing serial fields. Disconnect should mark telemetry
unavailable/last-known instead of retaining green “live” indicators.

Evidence: [raw transcript](transcript.txt), [structured results](result.json),
[first export](baseline-1.tpx3), [second export](baseline-2.tpx3),
[read-only runner](read_only.py). COM8 is closed/released.
