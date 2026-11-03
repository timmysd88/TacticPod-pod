# Supervised physical button and LED validation

Status: physical trial did not pass the complete acceptance criteria. Owner reports two attempts (antenna initially disconnected), no green LED, and continued blue pulses after attempted stop with USB disconnected. Archive contains five new clean sessions. Device now verified idle on COM8 with serial released; no host START, STOP, reset, reflash, erase, calibration or charging-setting change performed during diagnosis. Historical sessions preserved. See results below.

Owner confirms battery measured 4.12 V at BAT+/BAT-, button connected D0/P0.02 to GND, battery cool and connection good. This is owner-reported physical evidence, not an electrical measurement by the assistant. No calibration or charging-setting change authorized or planned.

Fresh read-only preflight saved in preservation-result.json, before-test-transcript.txt, before-test.tpx3 and before-test-decoded.json. COM8 identity matched the existing prototype. All recorders stopped, compressor queues empty, TCB idle/unlocked with no faults. JEDEC/status/SFDP matched prior fingerprint. Full archive decoded successfully; five historical sessions complete and unchanged from the post-upload backup. Serial released.

## Procedure — do not perform until explicit approval

1. Leave the button released. Keep prototype accessible, supervised and wiring secure; use an open view of the sky if feasible. No reset, reflash or calibration.
2. After approval and final idle preflight, disconnect USB while the already-connected battery supplies the device. Wait about 10 seconds without pressing the button; observe idle/LED state. Actual standby current is not measured.
3. Press and hold D0 continuously for a full 3 seconds, through the confirmation, then release. Expected two short blue flashes for accepted start followed by a longer blue recording confirmation. Start the timer at recording confirmation. If no confirmation or red flashes, release the button, reconnect USB and report; do not repeat START.
4. Observe approximately 30 seconds total. GPS acquiring: brief blue flashes about every 3 seconds. Fresh valid fix: successive pulses alternate blue/green. These are pulses, not continuous light. No fix means alternating colours cannot be validated. Do not infer recording solely from periodic blue pulses.
5. At approximately 27 seconds, make a quick tap of about 0.1–0.3 seconds, fully release for about 0.2 seconds, then press again within 5 seconds and hold continuously for at least 3 seconds through the stop confirmation. Release afterward. Button mode has no automatic 30-second cutoff; target duration is manually controlled.
6. Expected clean completion: two short green flashes, then LEDs off/standby. Do not remove battery power during finalisation. If confirmation is absent or red appears, keep power available and reconnect USB for diagnosis; do not start another session. Do not treat a failed stop gesture as success.
7. Reconnect USB, leave the button released and report observed start/stop patterns, blue/green pulses, approximate duration and any warmth/unexpected behaviour. If serial is absent, report before resetting so retained diagnostics can be investigated.
8. Assistant verifies all stopped states and fault counters, downloads a fresh TPX3 archive, verifies packet/page/block CRCs and footer accounting, checks five historical sessions/prefix byte-identical, and confirms exactly one new completed session. Calculate actual recording duration and expected counts from timestamps rather than requiring an exact manual 30.000 seconds. Expected nominal 30-second counts are approximately 3000 IMU, 300 RMC and 30 GGA; IMU spacing/rate and GPS fix/UTC evidence assessed separately.

Uncalibrated telemetry does not provide trusted battery protection. No calibration or battery-policy change is part of this procedure. Actual session and physical LED observations follow below.

## Actual observations and post-test evidence

Owner reported two tests because GPS antenna was initially disconnected; two quick blue startup flashes; no green with antenna disconnected or connected; stop gesture appeared ineffective because blue pulses continued. Owner confirms no battery disconnection/reset between attempts and USB disconnected during continued pulses. Exact gesture timing, each attempt's duration and post-test temperature were not supplied. No further test requested.

Immediately inspected device on COM8: TCB state=1, session=122, error=none, lock=0, programs=159; rejected/missed/full/CRC/incomplete=0. Compressor idle, queues empty and fault=0. Already stopped before any host stop command, so recover_stop.py sent no STOP. Current next append 0x084000. Read-only archive capture and GPS diagnostics completed; serial released.

Post-test TPX3 archive: after-test.tpx3, 591424 bytes, 2114 packets, SHA-256 `ddb6dec14503ccdeca5c81259cdad39fabc9331af1f8788a703aa3c37c5f1a91`. Existing recorded prefix and sessions 1,3,12,58,61 are byte-identical to the pre-test baseline. All ten sessions decode complete, including five new sessions below. Export/root/session issues empty; packet/page/block CRC and footer checks pass. Every new footer has reason=Clean, offered=committed, lost=0 and rejected/missed/full/CRC/incomplete=0. Thus recording did stop and finalise; a specific physical stop gesture cannot be matched to a footer without event timestamps from the owner. Five new sessions cannot be mapped reliably onto the owner's two described attempts.

| New session | Capture seconds | IMU samples | RMC messages | GGA messages | IMU observed Hz | RMC observed Hz |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 74 | 81.087 | 8060 | 408 | 82 | 99.392 | 5.030 |
| 96 | 17.756 | 1766 | 18 | 18 | 99.423 | 1.005 |
| 101 | 74.720 | 7427 | 74 | 74 | 99.390 | 0.994 |
| 119 | 10.910 | 1085 | 11 | 11 | 99.395 | 0.994 |
| 122 | 39.854 | 3962 | 40 | 40 | 99.408 | 0.994 |

Rates calculated as (count-1)/(last timestamp-first timestamp), with capture duration from metadata start to footer stop. IMU max intersample gap across these sessions 10.935 ms, minimum 9.067 ms; no 20 ms acquisition gaps. This is approximately 99.4 Hz, not an exact measured 100 Hz. GPS RMC fix_valid=0 in all new sessions. Green GPS-fix indication was therefore not expected; actual green LED electrical operation is still untested. No exact 30-second trial was captured.

## Diagnosed issues and limits

1. **Stop-confirmation event handling defect (source review).** src/main.cpp updateFieldControls calls the button stop path, then immediately observes recording()==false because finishing has begun. It updates wasTcbRecording to false before asynchronous finalisation produces takeCleanCompletion(). On a later loop, the green RecordingStopped notification requires wasTcbRecording still true, so clean completion can fail to trigger its two green flashes. Existing LED unit tests directly notify RecordingStopped and do not exercise this main-loop transition. This explains a missing stop confirmation without assuming flash finalisation failed. Firmware left unchanged.
2. **Periodic blue indication is not gated on recording.** updateFieldControls calls userLed.update for every awake loop; status_led.h emits blue/green baseline pulses regardless of recorder state. USB idle pulses are therefore ambiguous. Owner explicitly observed pulses off USB, so USB-awake behaviour alone does not establish the cause. Off-USB standby entry/button state was not instrumented; continued flashing and the five starts require follow-up. Do not claim the standalone button/standby test passed.
3. **GPS configuration changed during the trial; cached status is misleading.** Prior USB tests had RMC approximately 10 Hz. New session 74 contains a mixture of rates; later sessions approximately 1 Hz. Current read-only GPS CONFIG reports CFG-RATE-MEAS=1000 ms, NAV=1, GPS/Galileo/BeiDou enabled, GGA and RMC divisors=1; GSA/GSV/GLL/VTG enabled. These differ from the earlier RAM-only 100 ms GPS+Galileo profile. Live 15 s GPS RATE confirms RMC=15, GGA=15, both 1 Hz with advancing UTC, no valid fixes, checksum failures=0. Meanwhile GPS STATUS still prints cached '10HZ VERIFIED RAM'. Receiver version SPG 5.10 / protocol 34.10. Cause of receiver RAM-profile reversion is unestablished; owner reports no board reset/power disconnection. No GPS configuration write/reapply or OTP operation was performed. No antenna fault is proven by no-fix data alone.
4. **Uncalibrated battery.** Latest nominal 4118 mV / filtered 4119 mV, cell=0, calibrated=0, trusted=0, ADC timeout/invalid=0, usb_vbus=1. These are telemetry only; prior owner meter value 4.12 V remains the physical reference. No calibration or charging-setting changes made.

Result: data preservation, CRC integrity and clean session finalisation pass. Button/stop LED usability, off-USB standby indication and intended 10 Hz GPS operation do not pass full validation. No new hardware write/test initiated to diagnose these issues. Follow-up should address completion-event handling, recording-vs-idle LED meaning, GPS profile readback/recovery, and instrumented off-USB standby before another trial; any later upload/recording remains subject to explicit approval.

Evidence: button-failure-result.json/transcript, after-test-preservation-result.json, after-test-decoded.json, session-analysis.json, gps-followup-result.json/transcript, before-test.tpx3 and after-test.tpx3. No physical assertion inferred merely from code or simulations.
