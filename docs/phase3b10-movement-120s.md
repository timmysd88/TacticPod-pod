# Phase 3B.10 — bounded two-minute movement test preparation

Preparation and host simulation only. No firmware upload, hardware access,
physical recording, external flash program/erase or power interruption is performed
in this phase. The installed hardware remains on the used 30-second profile.

## Confirmed predecessor and separate build

The Phase 3B.9 corrected hardware report, identical 49344-byte exports and recovered
45056-byte image establish these preserved sessions:

| Session | Observations IMU/RMC/GGA | Sector range |
| --- | --- | --- |
| 1 | 500 / 50 / 5 | 0x000000–0x001FFF |
| 3 | 3000 / 300 / 30 | 0x002000–0x00AFFF |

The next exact start is **0x00B000**, and its session ID is
`0xB000 / 4096 + 1 = 12`. The complete two-session physical image is checked into
`tests/tcb3/fixtures/phase3b9-two-sessions.bin`, with its provenance manifest and
SHA-256 `ca12e165fa6ce7004f0ca00e60b2e975781834e56ed0d66b07511af70befc48e`.
Its bytes are used directly in the simulations, not regenerated approximations.

New environment: **tcb3_movement_120s**, explicitly enabled test profile **3**.
Existing default, five-second and corrected 30-second environment flags remain
unchanged. Runtime marker:
`test=120s,1MiB,180s profile=3B10 exact=B000`.
TCB3 container 3.2.0, frozen V2, V1, original sensor/timestamp precision, UART/GPS
configuration, flash partition, TPX3 and RECORD READ formats remain unchanged.

## Hard limits and sizing

| Guard | Limit |
| --- | --- |
| Initial append address | Exactly 0x00B000 |
| Initial session / sector sequence | 12 / 0 |
| First program | Header page at 0x00B000; next-sector counter 0x00C000 |
| Allowed new program bytes | 0x00B000–0x0FFFFF |
| Exclusive ceiling | 0x100000 (absolute 1 MiB address) |
| Last permitted page start | 0x0FFF00 |
| Capture duration | 120000000 us |
| Total capture plus finalisation deadline | 180000000 us from capture start |
| Allowed captures | One per boot; none automatic |
| Protected diagnostic sector | 0xFFF000–0xFFFFFF; never targeted |

The chip is **128 Mbit / 16 MiB**, not 128 MB. The 1 MiB absolute test ceiling
leaves 245 sectors / 1003520 bytes / 980 KiB available after the old 44 KiB prefix.
All erase paths and V1 write controls remain refused in the engineering build.

At nominal rates, uncompressed canonical data is
`120 × (100×24 + 10×44 + 1×40) = 345600 bytes`. An endpoint IMU sample can add
24 bytes without indicating a missed interval or a fabricated replacement.

Budget an extra boundary block: 61 blocks at the **hard maximum 8228 bytes**,
regardless of compression effectiveness. Each needs `ceil(8228/216) = 39` data
pages, so 2379 data pages and `ceil(2379/14) = 170` sectors. The fixed sector
layout includes header page 0, data pages 1–14 and reserved footer page 15.
Allocating 170 complete sectors already reserves all headers, commit/CRC bytes,
page padding and footer space. There is no need to allocate another footer sector.
The resulting allocation is 696320 bytes (680 KiB), ending exclusively at
**0x0B5000**, with 75 sectors / 300 KiB remaining before the fence. A compile-time
assertion checks this frame/page/sector budget against the window.

This budget assumes the requested 100/10/1 streams and bounded event traffic.
Unexpected traffic or event floods cannot bypass the address fence; they may
cause a reported capacity/queue stop instead of a successful full-duration run.
No ratio from stillness is used as a guaranteed capacity assumption.

The prior physical maximum block persistence was 393 ms. The maximum-frame
simulation budget includes 10 ms program BUSY and a 400 ms service blackout per
two-second period, with 1770 ms arrival-to-commit. Four conservatively queued
blocks would take about 7.1 seconds at that model bound; the extra 60-second
finalisation allowance is deliberately larger. These assumptions are not chip
electrical WCET guarantees. The existing 1-second individual program timeout and
hard 180-second total watchdog fault-lock on abnormal delays.

## Allocation, integrity and failure behaviour

The profile validates all eleven preserved sector headers in full: CRC, seal,
container, payload/flags, known session IDs 1 and 3, sector sequences 0–1 and
0–8, and matching metadata within each session. Metadata/time origins may differ
between the two sessions. Missing, corrupt or inconsistent preserved headers,
unreadable sectors, nonzero unexpected status or any occupied later header prefix
fault before allocation. The exact initial address is not moved to a later hole.

All 4096 bytes of the expected first sector are probed read-only. Any non-FF byte
faults; there is no skip/search fallback. Initial allocation rechecks 0x00B000.
Each entire sector is checked again immediately before its header, including
every continuation sector. Every destination page is blank-checked separately.
Before the first program, both the store and physical SPI adapter require the
exact address, session, sector sequence and next-sector counter. The adapter
checks before WREN. All operations retain range/alignment/status guards.

One held-CS 256-byte program includes page CRC32 and commit seal. WEL checks,
2-ms busy polling, timeout, full readback and CRC/seal verification remain.
The compressor input/output leases stay bounded, immutable while owned, and
fixed in RAM. Source rejection, missed acquisition intervals, flash-full and
CRC/write errors retain explicit counters and stop/fault handling. There is no
sample interpolation, timestamp rounding or lossy encoding.

Capture and total deadlines run independently of USB connection and foreground
service permission. Pending/active/completed repeated START is refused. After a
reset with an interrupted or complete new session, this exact-start profile faults
on the newly occupied prefix; it never resumes or starts another recording.
General decoding can recover previously committed blocks from an interrupted
image. No erase, retry or automatic recording is introduced.

The whole future allocation is not independently pre-scanned. Full old-session
payload/footer validation is a mandatory host preflight against archived TPX3
data; firmware checks its headers, not an embedded archive hash. Every new sector
is fully verified immediately before allocation. This is the approved verification
policy used by the preceding controlled trial.

## Simulation and resource evidence

`tests/tcb3/movement_120s_tests.py` executes production ARM main-loop, compressor,
SPI adapter, clock and store code under Unicorn with the actual two-session NOR
image. The model enforces erased bits, page bounds, one program per page, commit
validation and protected-region checks. Test arrays and enlarged USB capture
buffers exist only in the host harness; they are not firmware RAM allocations.

Coverage includes disconnected full capture, exact canonical source-byte recovery,
all three sessions independently decoded/exported, high-entropy six-axis data,
extra compression CPU cost, maximum-size uncompressed frames, flash BUSY periods,
explicit overflow/loss handling, first/continuation-sector occupancy, partial
programs, corruption of either predecessor, address/identity tampering, unreadable
sectors, unexpected status, repeated START, interrupted writes and refusal after
reset, TIMER4 low-word rollover, full address-window exhaustion and the 180-second
watchdog. Existing compressor suites also exercise forced raw fallback and hostile
precision extremes for each record kind, stillness, walking, sprinting, jumping,
direction changes, noise and the saved real recording fixture.

The maximum-frame fence test directly drives the store beyond the frontend's
120-second capture interval to exhaust all 245 sectors. It does not bypass the
duration guard in physical firmware.

Normal high-entropy model: 12001 IMU / 1200 RMC / 120 GGA, 60 blocks, zero losses,
898 programs, 60 new sectors at **0x00B000–0x046FFF**, combined TPX3 318144 bytes,
and exact reconstruction of 345624 new canonical bytes. Model block persistence
maximum is 358 ms, SPI service 544 us, input/output high-water 2/1. Injecting
100 us per compression unit and 10 ms BUSY also completes without loss (897
programs, 357 ms block maximum). Compression changes slightly with timing-derived
timestamps; both runs reconstruct their exact source records.

Maximum-size raw frames to the fence: 87 committed blocks, 3676 programs,
1770 ms maximum arrival-to-commit, queue high-water 1, 1370 ms store block maximum,
no violations, highest page 0x0FFF00. Extra offered data is explicitly reported
full/lost at the fence; existing sessions and all bytes at/above 0x100000 remain
unchanged, including the diagnostic canary.

The build allocates **120692 bytes static RAM** (50.8% of 237568) and **182792 bytes
program storage** (22.5% of 811008). Static RAM is unchanged from the tested
30-second profile. No dynamic allocation is added. Stack/heap high-water and
power consumption remain unmeasured on hardware. The preceding physical trial's
247 us compression-slice and 54.137 ms block CPU maxima provide context, not a
worst-case movement guarantee. New movement timing must be measured during the
separately authorised hardware trial.

## Build, review and proposed later hardware procedure

**All 20 regression suites passed**: the complete prior 18 plus the two-minute
movement suite and profile-3 USB command/diagnostic guard suite. Conditional GO
for a separately authorised, supervised 120-second movement test after review.
No physical test is performed by this preparation step.

Reviewed archive: `logs/phase3b10-review/tcb3_movement_120s-3b10.zip`.

| Artifact | SHA-256 |
| --- | --- |
| ZIP | `0bdd99698bd4535dfe02943a045d3c9b697a4f5f3d92b077857d7f4f8d9c059e` |
| ELF | `52e5bf1923e7cf6da9b9c06f04cd5e996ad88af5ff1ce48f38f027c4f537f798` |
| Application payload in ZIP | `d47576995adb23fb831502e4066148816c5aadd40328fe17ff2f189343868900` |

Production files changed: `platformio.ini`, `src/tcb3_test_limits.h`,
`src/tcb3_store.cpp`, `src/tcb3_session.cpp` and `src/flash_write_test.cpp`.
Host harnesses add `movement_120s.cpp`, `movement_120s_stress.cpp`,
`movement_store.cpp`, `movement_console.cpp`, `movement_120s_tests.py`,
`movement_controls_tests.py` and the two-session fixture/manifest under
`tests/tcb3/`. `integration.cpp` allows a larger host-only USB capture buffer;
`first_console.cpp` allows the same guard test to compile under profile 3.
No active acquisition, GPS receiver, compressor, V1 writer, binary format or host
decoder implementation is changed. Existing profile limits remain unchanged.

The example TPX3 contains three complete sessions. All three independently
decode, both old sector regions exactly match the physical archive, and the
standalone decoder reconstructs all 446424 canonical bytes exactly. The default
disabled ELF hash remains
`a83cbaabf9c0e18034a5f70a987539c77c574e429e9b6aa3a06caf6d1a836089`.

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e tcb3_movement_120s
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/tcb3/movement_120s_tests.py
```

Build artifacts and regression commands/results are saved under
`logs/phase3b10-review/`. Do not upload an older environment or substitute the
currently installed exact-2000 profile. The default TCB3-disabled ELF remains
byte-identical to its previous validated artifact.

After separate approval, verify the new ZIP hash, upload only the dedicated
profile using the established application-only nrfutil ZIP procedure, and confirm
the 3B10 marker, identity 0B4018 / 16 MiB, expected status, GPS 10Hz RAM profile and
RECORD STOPPED / OBS OFF / COMP OFF / TCB idle. A pre-start TCB READ must match the
archived 49344-byte combined export, hash
`8e22bf5ad18f8a98c8d9ef7535603f6d882c32d2a0fc97362375204b1cef3240`, and both sessions
must decode completely. Require recovered append 0x00B000.

Issue **TCB START once**. Require session 12, exact B000 and zero skipped sectors;
STARTED implies the complete initial-sector probe passed. Use stable power for
representative movement while sampling; no live-print flood is required. Monitor
TCB STATUS and COMP STATUS sparingly, allow the automatic 120-second stop and
require clean finalisation before the 180-second total deadline. TCB STOP remains
an early-stop control. Never issue another START on a fault or unexpected result.

Capture **TCB READ twice** as raw binary and compare hashes. Independently decode
all three sessions; compare both old sessions and their sector bytes to the
archive, verify approximately 12000/1200/120 observations with boundary tolerance,
UTC/fix masks, individual timestamps, stream ordering, CRC/seals, sequences and
offered/committed/lost footer accounting. No GPS fix must be invented if movement
conditions lose reception. All new addresses must remain inside B000–0FFFFF.

On reset, loss, queue overflow, address mismatch, unexpected occupied sector,
status warning, timeout or CRC/write error, stop the test and preserve contents
and logs. A fault lock may refuse export until reboot; do not automatically reboot,
erase or retry. Stop for review and release COM8. The chip's exact revision,
electrical behaviour, temperature/endurance and power-loss behaviour remain outside
this engineering trial's qualification.
