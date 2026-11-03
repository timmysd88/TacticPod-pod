# PR #5 USB physical-button recording test — 3 November 2026

Recording data PASS; procedure qualified by duration and a post-test GPS STATUS refusal. Exactly one session (162) was created by physical D0 actions. Capture lasted **72.491556 seconds**, longer than the requested approximately 60 seconds due to the interactive stop sequence. No further hardware actions are planned.

## Firmware and preflight

Existing PR #5 commit `3d4693207d459b1565af2e8b3955cd897f5d519c`, environment `tacticpod_reusable_usb`. No rebuild or upload. Archived application BIN remains 202,192 bytes, SHA-256 `e8015cd7f400a5fdfcad784d211d72482451468783c8c9160566bf5cb3f612d6`; archived application-only ZIP remains 203,012 bytes, SHA-256 `b6d37fcabadd341e7f732a6bb2af46054387b9ea8a08ca8955befc52c05334a5`.

COM8 identity: VID:PID 2886:8044, USB serial `0E8A89B7289430BD`. USB VBUS confirmed. Recorder idle, observations and compression capture inactive, no export in progress. GPS: 38400 baud, valid NMEA, 2,642 valid checksums / zero failures; current RAM profile verified 100 ms, RMC 10 Hz, GGA 1 Hz, GPS+Galileo. Flash: five identical JEDEC `0B 40 18`, two matching reference-valid 256-byte SFDP reads, SR1=00 before/after, SR2=02. `qualification=UNRESOLVED` was informational.

Before-test inventory: sessions 1, 3, 12, 58, 61, 74, 96, 101, 119, 122, 133, 137, 144, 159. Session 133 already had an interrupted final block at 0x087000; retained unchanged. Complete recording-area TPX3 backup verified against the restoration baseline. Reconstructed image: 659,456 bytes; SHA-256 `d1c398b03deee2e8ea88a86779e856154ab0dfbac4a1165df1f62faf678e0379`.

## Physical operation and LED observations

Owner instructed to hold D0 for 3 seconds then release, with USB connected. Owner confirmed solid green followed by green pulses about every 2 seconds. Firmware trace records one start acceptance and one session creation. It also records additional button activity during preparation, before capture; no second start or second session resulted. First hold lasted approximately 9.2 seconds, including preparation. Capture timing used actual session start, not button acceptance.

Host issued the stop cue at approximately 54 seconds of capture. Owner physically tapped D0, released, then held D0. Trace: 160 ms tap, 185 ms gap, stop accepted after 3,000 ms of second hold. Owner confirmed two green stop flashes then blue idle pulses. Clean finalisation followed stop acceptance by 252 ms. No host START/STOP command or simulated button action was used.

## Export and offline verification

| Check | Result |
|---|---|
| Session | 162, complete, one new session only |
| Capture | 72.491556 seconds |
| IMU | 7,250 samples; 100.003777 Hz; gaps 9,338–10,917 µs |
| GPS RMC | 725 records; 9.999978 Hz; gaps 79,462–120,008 µs |
| GPS GGA | 73 records; 1.000012 Hz |
| GPS fix availability | 697/725 RMC fixes valid; 70/73 GGA quality nonzero; brief loss of navigation fix preserved explicitly |
| Timestamps | Monotonic; RMC UTC strictly advancing, 2026-11-03 11:41:25.400–11:42:37.800 UTC |
| Footer | Clean reason 0; offered=committed=8,048; lost/rejected/missed/full/CRC/incomplete all zero |
| Integrity | TPX3 sequence/packet CRC, page commits/CRC, compressed block CRC, record semantics and footer accounting passed |
| Earlier recordings | All 14 sessions identical; entire old recording-area prefix byte-identical |
| New issues | None; existing session 133 interruption unchanged |
| Final state | Recorder stopped/idle, lock=0, no error; RAM capture off and queues drained |

Post-test flash again passed all JEDEC/SFDP checks. Post-test TPX3: 806,464 bytes, SHA-256 `cf43251fd301f1eac71b5bd85c2850d195de5f9c2741956554abb2fbcbfbb934`. Reconstructed recording area: 737,280 bytes, SHA-256 `75be48ff3152051a9213a47cbe8f1237455bc6d683c2ae40e3722353ef552607`.

## Limitation and stop condition

The post-test `GPS STATUS` request was refused with “GPS profile operation active”; the outstanding normal read-only profile operation then reported CURRENT RAM PROFILE VERIFIED and UART=38400. FIELD EVENTS output was delayed by that operation and arrived in the same captured response; offline parsing recovered all events, with overwritten=0. The recording itself independently verifies continuous reception at the expected rates, but no successful standalone post-test GPS STATUS response was obtained.

The collection helper caught that status timeout internally and continued with read-only flash identification, export and idle checks before the refusal was surfaced for review. This did not meet the instruction to stop immediately on unexpected behaviour. No further device commands were issued after reviewing it. No retry, recovery, reset, additional recording or GPS backup experiment was performed. Treat this as a successful recording-data check with a qualified procedure, rather than an unconditional all-checks pass.

## Evidence

- `preflight-result.json`, `preflight-serial.txt`, `before-test.tpx3`, `before-test-image.bin`, `before-test-decoded.json`: readiness, original inventory and backup.
- `live-serial.txt`, `live-state.json`: complete recording status transcript and host timing.
- `post-test-serial.txt`, `collection-result.json`: delayed field events, status refusal/readback, flash health and final idle state.
- `after-test.tpx3`, `after-test-image.bin`, `after-test-decoded.json`: complete post-test export.
- `session-162.json`, `session-162-wire.bin`: isolated decoded session and reconstructed wire records.
- `analysis.json`, `analyse.py`: offline checks, measured rates, exact button events and command audit.
- `trial.py`: serial collection helper used; `SHA256SUMS.txt`: evidence checksums.

No erase, firmware change, rebuild/upload, reset, automatic GPS standby change, push, merge or release was performed. Serial port is closed. Review these results before further testing.
