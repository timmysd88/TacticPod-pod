# Idle battery optimisation ? phase 1

Review branch: `feature/idle-power-phase1`, based on fetched `origin/main`
`81abe0514d906a7cc997d7a3c705c704830acd56` (17 November 2026).
No pod connection, reset, upload, recording erase, commit, push or merge was used.
This document describes an unflashed implementation, not measured hardware results.

## Protocol evidence and supported receiver

Primary manufacturer documents:

- [M10 SPG 5.10 Interface Description, UBX-21035062 R03](https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf), section 3.16.6, pp. 116?117: PMREQ has class/ID `02 41`, **16 payload bytes**, version 0, three reserved zero bytes, duration U4, flags X4, wakeupSources X4, all little-endian. Duration 0 waits indefinitely for pin wake. Backup is flags bit 1, force bit 2; UART RX is wakeupSources bit 3. Other described sources are EXTINT0 bit 5, EXTINT1 bit 6, SPICS bit 7; they are **not enabled here**. A rising or falling edge on an enabled input wakes the receiver.
- [MAX-M10S Integration Manual, UBX-20053088 R04](https://content.u-blox.com/sites/default/files/MAX-M10S_IntegrationManual_UBX-20053088.pdf), sections 2.1.1.1 and 3.6.3.2, p. 42: VCC/V_IO remain supplied during software standby; force is required; entering standby clears RAM configuration; BBR/RTC are retained. The MAX-M10S factory UART is 9600. Pin wake does not constitute command reception or proof of readiness.
- Interface Description CFG-VALSET/VALGET definitions and sections 4.3?4.7: CFG-VALSET RAM layer bit 0; transactionless version 0 can apply a single key; VALGET RAM layer is 0 and response version is 1. Baud key `CFG-UART1-BAUDRATE` is U4 `0x40520001`.

Repository evidence: `docs/gps-10hz.md`, `docs/gps-config.md`, `src/gps_profile.cpp` and archived GPS reports establish the expected SPG 5.10 receiver, UART 38400 and RAM-only 100 ms navigation profile. No live configuration was queried in this task. Standby is gated on a successfully received SPG 5.10 MON-VER identification, rather than assuming all M10 firmware revisions implement the same command.

Exact sleep frame (including UBX header/checksum):

```
B5 62 02 41 10 00 00 00 00 00 00 00 00 00 06 00 00 00 08 00 00 00 61 6B
```

No GPS reset, CFG-CFG save, BBR/flash/OTP write or change to external power wiring is used. PPS is not a power-enable input. Software standby is not physical battery isolation.

## Behaviour

The reusable USB/BLE profiles use actual receiver standby. The default engineering build remains outside this policy.

1. Existing application settle/wake locks still govern standby. GPS adds independent checks for recorder/export/capture, diagnostics, active UART/profile work, supported firmware and completed prior TX.
2. Flash enters its existing WIP/WEL-guarded deep-power-down. A refused GPS suspend releases flash again before returning to awake operation. IMU/TIMER4 are suspended only after GPS command transmission succeeds.
3. PMREQ is one 24-byte DMA batch at 38400. The caller allows up to 20 ms, yielding with 1 ms delays while checking actual TX completion (wire time is 6.25 ms). Short/stuck TX starts bounded recovery and refuses standby. The existing core's RX/TX stop sequence is retained; no framework source is patched.
4. After TX completion, UART is stopped and TX parked HIGH without a deliberate wake edge. This records **standby requested**, not electrically confirmed standby; no PMREQ ACK is assumed.
5. A deliberate recording hold or existing USB wake resumes peripherals. The GPS wake controller alternates two attempts at 38400 and 9600, with a sacrificial one- or two-byte `FF` burst generating the documented UART RX edge. The first byte is not assumed to be processed by GPS. Burst length differs from the retained DMA AMOUNT, preventing a stale completion from hiding a newly stalled TX. Each probe requires a checksum-valid SPG 5.10 MON-VER response.
6. If found at 9600, a single RAM-only baud VALSET requests 38400. The host waits for TX completion and switches back to 38400. A lost baud-change ACK does not prove failure or success: MON-VER and RAM baud readback at 38400 must succeed.
7. Existing profile audit/recovery then verifies every recording-profile guard and, when necessary, snapshots the **current** receiver settings, reapplies the existing 10 Hz profile with ACK/readback/rollback, and verifies it. The old MCU-side snapshot cannot authorise recording after sleep. Fix reception is refreshed too.
8. Recording remains pending, with no capture/allocation/programming, until GPS readiness and the existing battery/capacity/storage admission checks pass. Failed UART/profile recovery cancels that request. A later explicit start can retry; nothing starts automatically when communication returns.
9. MCU-only reset can leave GPS asleep. If ordinary startup has no valid NMEA after 1.5 seconds and no diagnostic owns the receiver, the same bounded recovery runs once. No receiver reset is used.
10. Wake processing has a 10-second overall deadline; response probes have 1.5-second windows. The 100 ms inter-operation gaps are host pacing choices, **not a manufacturer-guaranteed readiness time**. Existing full profile preparation retains its 90-second admission bound. Unsupported firmware, malformed/wrong-layer/wrong-key responses, timeout and short/incomplete TX fail closed.

Double-tap BLE entry leaves GPS asleep; downloads use the unchanged transport, queues, connection parameters and flash reader. BLE exit still follows the existing reset path, which now also handles a GPS left asleep. Button gestures, USB/DFU, hardware pin mappings, storage formats and storage engine are unchanged. System OFF is not implemented.

## ADC scheduling

The existing backend advances one register phase per call and retains its 20 ms conversion timeout, five-conversion trimmed mean, 500 ms burst limit, one-second sample cadence, three-second freshness and warmup/admission checks.

`BatteryRuntime::idleDelayMs()` caps foreground delay at 1 ms while an ADC phase or burst is in progress; otherwise the existing 50/10 ms button pace remains. An active conversion is drained even if cell mode has been disarmed; absent-cell mode does not initiate new idle measurements. BLE uses the same cap while its normal download pace remains 1 ms. No timeout, freshness threshold, calibration requirement or low-voltage admission limit is relaxed. Cell mode and calibration remain RAM-only, as before; reset still requires operator re-arming/calibration when using armed-cell protection. This avoids sleeping 50 ms before servicing a conversion that times out after 20 ms.

## Validation

Compile-only commands (never `upload`):

```
pio run -e seeed_xiao_nrf52840_sense -e tacticpod_reusable_usb -e tacticpod_reusable_ble
$env:PYTHONPATH = Join-Path $PWD '.pio-core/test-python'
python tests/reusable/run_all_tests.py
```

The regression runner includes `tests/reusable/idle_power_tests.py`. Its independent protocol driver checks the golden PMREQ frame, both baud paths, lost ACK, wrong firmware/readback/layer/key, missing responses, short/stuck TX, timer wrap and repeated wake. Production-loop tests cover actual PMREQ, flash rollback, pending recorder/RAW ownership, MCU-only reset recovery, failed wake plus explicit retry, unchanged historical recording prefix and repeated standby cycles. Expanded real-register ADC tests exercise idle pacing, trust/freshness, low-battery refusal and the old 50 ms timeout as a negative control. Existing field/BLE suites cover button behaviour and download preservation.

Build and suite results are recorded after validation below. Generated logs and artifacts are in ignored `.pio/`; tests compile/emulate and never open device ports.

## Required hardware qualification before acceptance

- Confirm actual breakout model, VCC/V_IO/backup wiring and receiver firmware; verify measured sleep current at the battery and GPS rail. Breakout regulators, LEDs and antenna circuitry remain powered.
- Observe TX during PMREQ, UART shutdown, wake and baud transitions; confirm the parked line does not wake GPS inadvertently. Check PMREQ rejection/continued NMEA and verify the receiver actually sleeps.
- Measure both baud recovery paths and full verified-profile time, then validate real 10 Hz RMC / 1 Hz GGA alongside 100 Hz IMU acquisition. Mock timing is not a physical timing guarantee.
- Check calibrated-cell idle ADC samples, timeout/noise counters and freshness across the full hold gesture; verify low-battery admission remains blocked.
- Repeat button hold, cancelled hold, double-tap BLE, download, USB insertion/removal and BLE-exit reset cycles; include extended idle across millisecond-counter wrap. Measure actual BLE throughput against the existing firmware.
- Compare archived recordings byte-for-byte before/after testing. Confirm clean finalisation and read-only recovery; use separate sacrificial storage for power-loss fault qualification. Never use a sleep/wake path to erase or reset recordings.
- Test missing GPS, rejected baud configuration and interrupted UART transfers: no capture, allocation or programming before verified recovery, and explicit user retry required after refusal.

No measured current or battery-life claim is made by this phase.

## Changed files

| Files | Purpose |
| --- | --- |
| `src/gps_power.h` | Documented PMREQ encoding and bounded UART wake/baud verification controller. |
| `src/gps_receiver.cpp`, `src/gps_receiver.h` | Guarded receiver suspend, wake integration, startup recovery, diagnostic exclusion and power status. |
| `src/gps_config.cpp`, `src/gps_config.h` | Reuse checksum-validated UBX frames and reset parser across controlled UART transitions. |
| `src/gps_profile.cpp`, `src/gps_profile.h` | Require observed SPG 5.10 identity before standby and identify before first field audit. Existing 10 Hz transaction/rollback targets remain unchanged. |
| `src/main.cpp` | Ownership preflight, flash rollback on refused GPS suspend and ADC-aware standby/BLE pacing. |
| `src/battery_runtime.cpp`, `src/battery_runtime.h` | Expose short pacing while the existing ADC state machine is active. |
| `tests/reusable/gps_power.cpp`, `tests/reusable/idle_power_integration.cpp`, `tests/reusable/idle_power_tests.py` | New protocol and production-loop regression cases. |
| `tests/reusable/adc.cpp`, `tests/reusable/adc_tests.py` | Real backend standby sampling, stale/low admission, timeout negative control and disarming. |
| `tests/reusable/integration.cpp` | Independent powered/sleeping receiver model, including lost RAM and baud-change ACK. |
| `tests/reusable/ble_field_integration.cpp`, `tests/reusable/ble_cache.cpp` | Run actual receiver identity/profile startup before requesting standby; allow bounded profile recovery before cache-readiness assertions. |
| `tests/reusable/run_all_tests.py` | Include the new idle-power suite. |
| `tests/recording/harness.cpp`, `tests/flash_write/harness.cpp` | Update isolated GPS stubs to the guarded boolean suspend interface. |
| `docs/idle-power-phase1.md` | Protocol evidence, implementation review, validation and physical qualification plan. |

The storage engine, formats, flash-power helper, BLE runtime/transport, pin mappings,
PlatformIO configuration, linker scripts and installed framework source are unchanged.

## Final validation results ? 17 November 2026

- **47/47 regression suites have passing results**, including all **27 new GPS power cases** and **14 real-backend ADC cases**.
- The first complete runner reported four failures: two old test stubs still declared `void suspend()`, and two standalone GATT/ELF scripts could not import `pyelftools` from this interpreter. The stubs were corrected; both affected suites passed on rerun. The GATT/ELF checks passed with the existing pinned `.pio-core/test-python` directory on `PYTHONPATH`, as shown in the command above. No dependency installation or framework edit was needed. Initial logs are retained rather than relabelled.
- Final build results:

| Environment | Result | RAM bytes | Flash bytes |
| --- | --- | ---: | ---: |
| `seeed_xiao_nrf52840_sense` | PASS | 119000 | 167964 |
| `tacticpod_reusable_usb` | PASS | 123468 | 224288 |
| `tacticpod_reusable_ble` | PASS | 144784 | 337036 |

Evidence: `.pio/idle-power-builds.log`, `.pio/idle-power-regressions.log`,
`.pio/reusable-regression-logs/`, the four `.pio/idle-power-*-rerun.log` files,
and consolidated `.pio/idle-power-validation.json` (initial results, corrected
reruns, final results, build sizes and source hashes). The complete runner ran
all 47 suites; its initial exit status was nonzero for the four resolved issues
above. The final source also passes `git diff --check`.

The working tree contains 22 intentional changed/new files, all listed above,
on the feature branch; nothing is staged or committed. Protected storage,
flash-power, BLE runtime, board/variant, linker and PlatformIO sources have no
diff from the fetched main. All hardware qualification remains outstanding;
this is a review candidate, not permission to flash or a measured power result.
