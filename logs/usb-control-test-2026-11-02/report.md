# USB-connected physical button control test

Existing merged firmware, main 155a999122f0ce85ab6817c6eba690e9501fe4b3.
No reflash, host START/STOP, erase, calibration or charging-setting change.
Owner requested one additional test and confirms USB connected throughout,
normal blue/green and green-stop indications, no red or unusual warmth (reply:
"yes, no red or unsual warmth").

Baseline: recorder idle, correct COM8 identity, complete TPX3 export identical
to the previous verified archive. Twelve existing sessions including known
interrupted session 133 are preserved; expected next ID 144.

## Actual session 144

- Exactly one new recording, complete with clean footer, duration 58.536642 s.
  This is longer than the requested approximately 30 s; report actual duration.
- IMU 5854 samples, observed 100.005816 Hz, gaps 8.860–10.886 ms.
- RMC 585 messages, observed 10.000574 Hz; all 585 have valid fixes and advancing UTC.
- GGA 59 messages, observed 1.000031 Hz; all 59 have fix_quality=1.
- Offered=committed=[5854,585,59,0], lost=[0,0,0,0]. Footer rejected, missed,
  CRC failures, incomplete writes and full counters all zero; clean reason=0.
- No new session/container integrity issues; transport packet CRC/sequence
  checks and decoder page/block/footer checks pass. The pre-existing interrupted
  final block at 0x087000 in session 133 remains unchanged, not a new failure.
- All 12 previous sessions and their recorded flash prefix byte-identical.

## Button/event evidence

| Event | Boot t_ms |
| --- | ---: |
| Start press | 911001 |
| Start accepted | 914001 |
| Start release | 914362 |
| GPS verified | 920073 |
| Session 144 created | 922004 |
| Stop tap press/release | 977126 / 977328 |
| Stop sequence armed | 977328 |
| Second stop press | 977542 |
| Stop accepted / finalising | 980542 |
| Clean footer completion | 980695 |
| Final release | 981214 |

One start_accepted, session_created, stop_armed, stop_accepted, finalising and
finalised_clean; three presses/releases; zero fault events and overwritten=0.
Accepted start to actual capture/session creation delay 8.003 s. The stop tap
lasted 202 ms, release gap 214 ms and second hold 3000 ms. Footer completion
followed stop acceptance by 153 ms. No unexpected restart/interruption detected
in this run. USB kept the board awake, so absence of standby entry is expected;
this does not test battery handover or off-USB standby.

The live host logger stopped during startup because the firmware replaced an
expected TCB STATUS reply with asynchronous START/STARTED reports. This was a
host-reader limitation, not an observed firmware crash. Original monitor log
and error retained. Post-test read-only inspection captured the intact RAM
event trace, confirmed idle, and downloaded the complete archive. No host STOP
was needed; serial released after capture.

Archive: after-test.tpx3, 707904 bytes, SHA-256
c403dbd1d0fe1bb3a40ed38d5258b9a2992f0f3bc2b14376cf066854cf2a23e0.

Result: USB-connected button start/stop, clean finalisation, sample/message rates
and data preservation pass for this single approximately 59-second recording.
It is not an exact 30-second duration result. Earlier battery-only interruption
is not reproduced or explained by this control test; do not conclude a specific
battery, wiring or firmware cause from the difference alone.

Evidence: analysis.json, before-test.tpx3, after-test.tpx3,
post-test-state-events.json/transcript, after-test-decoded.json,
live-transcript.txt and monitor-result.json. No further test initiated.
