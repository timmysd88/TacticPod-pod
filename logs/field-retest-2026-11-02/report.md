# Merged firmware upload and 30-second instrumented retest

Firmware main 155a999122f0ce85ab6817c6eba690e9501fe4b3, application BIN SHA-256
40bced328dbcc453eb8326297fabab40c268063140d6c909a4359b4024a9a585.
Owner requested upload and a user-performed 30-second test; confirms antenna
connected, button released, cool/secure battery and unchanged verified polarity.
Prior measured cell reference 4.12 V; no new meter measurement or calibration.

Pre-upload stopped-state/identity checks passed. Complete baseline TPX3 contains
sessions 1,3,12,58,61,74,96,101,119,122; next append 0x084000. SHA-256
ddb6dec14503ccdeca5c81259cdad39fabc9331af1f8788a703aa3c37c5f1a91.

Verified application-only DFU ZIP c38561e5004ec88bf5d1b6b8b21c4d50a55cbbd19db41a7065c804438a6ae007,
matching BIN hash and S140 requirement 0x0123. One existing 1200-baud bootloader
entry, then one adafruit-nrfutil application upload via COM7: exit 0; application
reconnected on COM8 with original serial 0E8A89B7289430BD. No bootloader/SoftDevice
component, external-flash erase, charging change, calibration or host START.

Post-upload checks passed: all recorders idle, compressor empty, flash identity
and status unchanged; IMU returned 20 live readings in two seconds. Current
GPS CONFIG readback checked MEAS=100 ms, NAV=1, RMC divisor=1, GGA=10,
GPS/Galileo enabled, BeiDou disabled, UART=38400 and GSA/GSV/GLL/VTG outputs off.
GPS RATE measured 149 RMC / 15 GGA in 15 s (9.933 / 1.000 Hz), all with valid
fixes and advancing UTC; checksum failures 0. Historical TPX3 archive and its
reconstructed image remain byte-identical. USB reopening stayed idle/programs=0.
The earlier GPS STATUS cumulative counter contained one checksum failure since
boot; the subsequent 15-second rate window had zero. This is separate from flash
and TPX3 CRC counters, which passed their checks. Battery telemetry remains
uncalibrated/unarmed (nominal 4123 mV / filtered 4124 mV); no voltage protection
claim is made.

FIELD EVENTS baseline drained and saved in field-events-pretest.txt with
overwritten=0. Serial released. All raw diagnostics and exact command records
are retained here. A physical test has not yet been reported or analysed.

Intended single physical test: disconnect USB and wait 10 s; one full 3 s hold,
release after accepted-start two blue flashes; wait for longer capture-start
blue confirmation (GPS readback may delay it), then time approximately 30 s.
At about 27 s, tap 0.1–0.3 s, release about 0.2 s, hold again for at least 3 s
through the two green clean-finalisation flashes, then release. Keep battery
power available, wait 10 s, reconnect USB and report observations. Do not repeat
START or reset if confirmation is absent; reconnect for diagnosis. Subsequent
analysis must verify exactly one session (expected 133 only if baseline remains
unchanged), one accepted start and clean completion, timestamps/counts/rates,
CRCs/fault counters, GPS profile/fix evidence and all ten old sessions unchanged.

## Actual result: first recording interrupted, second clean

Owner reports first attempt appeared to stop by itself after roughly 20 seconds,
no red indication, no manual RESET and no battery disconnection. Two new sessions
were found, rather than the intended single 30-second session. The recorder was
already idle/unlocked with session 137 when inspected; no host STOP was sent.

| Session | Outcome | Capture duration | Recoverable IMU | RMC | GGA |
| --- | --- | ---: | ---: | ---: | ---: |
| 133 | Interrupted; footer absent; incomplete final block | Unknown; committed IMU spans 9.995 s | 995 | 100 | 10 |
| 137 | Complete, clean footer | 20.352622 s | 2025 | 202 | 20 |

Session 133 did not make a safe automatic/timed finalisation. Its missing footer
and interrupted final block show an abrupt interruption; its actual end time,
uncommitted samples and final fault counters cannot be recovered from a footer
that does not exist. Valid committed data remains accessible. No hardware reset
reason, persistent boot counter or power measurement is exposed by the current
supported console commands, so a particular reset/power cause is not proven.

There is strong evidence consistent with an unexpected restart: session 133's
start timestamp is only 19.209630 s, earlier than the previous pretest boot's
later diagnostics; the retained RAM trace contains fresh startup GPS verification
events at 17.113/23.211 s and only the session 137 lifecycle, with overwritten=0.
The earlier 133 start/stop history is absent. Session 137 starts at 41.279688 s
in its clock domain. No host reset, upload or clock-changing command was issued
after the approved upload. The owner denies deliberate reset/power removal.
Do not equate this evidence with a proven brownout, watchdog, battery fault or
specific software crash; reset-cause instrumentation is needed to distinguish
these and to rule out clock/event-history faults.

Retained session 137 event timeline (boot millis):

| Event | t_ms |
| --- | ---: |
| Start accepted after continuous hold | 32859 |
| GPS RAM verified after wake | 38996 |
| Session 137 created | 41099 |
| Stop tap press / release / armed | 57842 / 57997 / 57997 |
| Second stop press | 58342 |
| Stop accepted / finalisation started | 61343 |
| Clean finalisation completed | 61610 |
| Button released | 62147 |
| Return to standby | 69425 |
| USB wake | 76549 |

This proves a physical button stop for the second session; it was not a host
stop or an autonomous time-limit stop. Footer completion followed acceptance by
267 ms. Start acceptance preceded capture/session creation by 8.240 s; timing
from the first two blue flashes rather than actual capture confirmation could
explain why the recorded interval was closer to 20 s than 30 s. This is an
interpretation of the timing, not a claim about when the owner started a timer.

Recovered IMU rates are 99.454 Hz (133) and 99.470 Hz (137). Session 137 IMU gaps
are 9.363–10.950 ms. RMC rates are 9.941 / 9.947 Hz; GGA 0.995 / 0.995 Hz.
All 100 and 202 RMC observations have valid fixes and advancing UTC; all 10/20
GGA observations have fix_quality=1. GPS stayed near the intended message rates
in both recovered intervals. RMC/GGA quality evidence does not by itself prove
the physical LED colours observed by the owner.

Session 137 offered=committed=[2025,202,20,0], lost=[0,0,0,0], clean reason=0;
rejected/missed/full/CRC/incomplete footer counters are zero. Current diagnostics
also show idle, error=none, lock=0, compressor drained and no current fault.
These zero counters describe the completed second session, not the interrupted
first session whose final counters are unavailable. Battery remains uncalibrated
and unarmed (cell=0/calibrated=0/trusted=0), so trusted low-battery auto-stop was
not enabled by the assistant. No calibration or charging change was made.

Both complete TPX3 exports are byte-identical: 640704 bytes, SHA-256
bf6711fe647f8607e671f5bf4c9905c3ab80b3ba59fc3e9a465db7f13ff4e39e.
Transport packet CRC/sequence checks pass. All ten pretest sessions and their
recorded flash prefix are byte-identical. There is one actual container/session
issue at address 552960 (0x087000): interrupted final block in session 133.
This is stable stored evidence, not a truncated USB download. Export ISSUE list
is empty; do not misreport the entire archive as issue-free.

Result: session preservation and second-session button stop/finalisation pass;
the overall single-session 30-second battery test fails because of the first
interruption and the different duration. Pause further physical tests pending
unexpected-restart investigation. No automatic retry, reset, reflash, erase or
new recording was performed during this analysis. Serial released.

Evidence: post-test-state-events.json/transcript, after-test.tpx3,
after-test-confirmation.tpx3, after-test-decoded.json and session-analysis.json.
