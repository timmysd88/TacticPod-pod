# Phase 3B.8: bounded 30-second append test preparation

**Prepared only. No upload, serial-device access, physical recording, erase or
program operation was performed in this phase.** The physical first session
remains in sectors `0x000000–0x001FFF`, and the installed firmware remains the
five-second profile. Do not use its START command for this proposed test.

## Decision and separately versioned profile

Conditional **GO for a supervised 30-second engineering test after review and
separate upload/write approval**, using only the new dedicated artifact. No GO
for unrestricted capture or power-interruption testing is implied.

New environment: `tcb3_append_30s`, test profile **2 / 3B8**. It explicitly sets
TACTICPOD_TCB3_FLASH_ENABLED=1 and TACTICPOD_TCB3_TEST_BUILD=2 and inherits the
existing board/IMU flags. Profile 1 (`tcb3_first_write`) remains five-second,
64 KiB, start-at-zero-only. The normal default environment remains disabled;
the default application ELF is unchanged from the installed disabled baseline.
This versions the engineering build, **not the container**: TCB3 remains 3.2.0,
typed V2 records and V1/V2 decoding remain unchanged.

| New-profile limit | Enforced value |
|---|---|
| Capture | 30,000,000 monotonic microseconds |
| Finalisation deadline | 60,000,000 us total from capture start |
| Protected first-session floor | No TCB program below `0x002000` |
| Absolute write ceiling | Exclusive `0x040000`; maximum byte `0x03FFFF` |
| Last permitted page start | `0x03FF00` |
| Available test window | 62 sectors / 253,952 bytes between floor and ceiling |
| Diagnostic sector | `0xFFF000–0xFFFFFF`, excluded from all test writes/erases |
| Repeat/boot behavior | One START per boot; no recording at boot or automatic resume |
| Other mutations | V1 START and all erase commands refused in the test build |

STATUS marker: `test=30s,256KiB,60s profile=3B8 min=2000`, followed by skipped
candidate count. 256 KiB is the absolute address ceiling from flash zero, not
256 KiB of newly allocated storage. The floor preserves the original 8192 bytes
even if an allocation/state error attempts an earlier address; both core page
writer and SPI adapter reject out-of-window or unaligned addresses before WREN.

Deadlines are the same USB-independent mechanism validated in Phase 3B.7,
parameterised by profile. Observation admission stops at the capture cutoff;
foreground stop/footer service can occur slightly afterward. Drain may continue
only until the total deadline, when retained/in-flight work faults and locks.
An already-begun held-CS partial program can be initiated by releasing CS during
fault cleanup, inside the fence; it is not retried or declared a clean page.
These are firmware foreground deadlines, not a CPU-lockup-proof hardware watchdog.

## Read-only allocation and preservation

The existing partition is unchanged: recording exclusive end `0xFFF000`, final
diagnostic sector excluded. The full header-prefix scan reserves every occupied
header, including V1/V2/TCB3, corrupt and torn prefixes. It chooses a candidate
**after the highest occupied sector**, never a hole below it. Profile 2 then
enters a new read-only Probe state before capture can start:

1. Refuse if recovered append position is below the `0x2000` preserved-session
   floor, or at/above the `0x40000` ceiling. Missing prefix data is not treated as
   permission to start at zero.
2. Check flash status and read the entire candidate sector in 64-byte slices.
   No WREN, header program or capture begins during this probe.
3. If any byte is non-FF, reserve/skip that sector and inspect the following
   sector. This handles old payload below a blank/torn header prefix without
   overwriting or reusing it. Stop safely when the window is exhausted.
4. Only an entirely FF sector becomes a verified candidate. START then creates
   a new session ID from that sector index plus one. The sector and each page
   are checked again before the first write, protecting against changes after
   the read-only probe.

`TCB STOP` during scan/probe cancels read-only allocation and invalidates the
scan result; a partial scan cannot subsequently allocate. Unknown status faults
before allocation. No erase or protection/configuration adjustment is performed.

With the known first-session image, the expected new start is **`0x002000`**,
session ID **3**. This has been proven using its exact archived 8192-byte image
in simulation; it is a prediction, not a new physical blank-sector measurement.
The approved hardware run must compare the old export with Phase 3B.7 before
starting. If further occupied sectors exist, allocation moves forward and the
actual start/session ID must be recorded. Corrupt existing data is not repaired.

A reset starts a new timestamp domain and scans read-only. It never resumes the
interrupted session; a later explicitly authorised START appends after it. The
RAM one-start latch resets with the MCU, so it is not a persistent prohibition
against all future manual sessions. The hard write window persists through the
build constants. The future host runner must still enforce one approved START
and no automatic retries. Device timestamps are monotonic within a recording
boot/session; do not concatenate timestamps across reset as one continuous clock.

## Capacity and timing calculation

Nominal 30-second raw typed payload:

| Source | Count | Bytes each | Total |
|---|---:|---:|---:|
| Six-axis IMU | 3000 | 24 | 72,000 B |
| RMC | 300 | 44 | 13,200 B |
| GGA quality | 30 | 40 | 1,200 B |
| Total | 3330 | | **86,400 B** |

Normal cadence fits 222 observations / 5760 raw bytes per two-second block,
below the 256-record / 8192-byte limits. The source window normally makes 15
blocks. Budgeting an extra partial boundary block gives 16. Conservatively
budgeting **all 16 at the hard 8228-byte frame maximum**, each needs 39 fragment
pages: 624 data pages, 45 sectors including reserved header/footer layout,
184,320 allocated bytes. Starting at `0x2000` predicts an exclusive end of
**`0x02F000`** (last byte `0x02EFFF`), leaving 17 sectors before the fence.
This is a conservative normal-rate budget, not a guarantee for arbitrary event
floods; the fence always stops additional allocation safely.

Phase 3B.7 physically measured maximum block persistence diagnostic 369 ms,
foreground flash service 1108 us and compression slice 219 us, without losses.
The 60-second total deadline allows 30 seconds for draining after capture ends.
That is ample relative to those measurements and the declared stress model,
while still bounding a stalled finalisation. It is not an unconditional tPP or
CPU WCET guarantee; a timeout intentionally fails rather than extending the
deadline or fabricating samples.

The new maximum-frame pacing model reserves 400 ms of no-store service per
two-second period and uses 10 ms program BUSY. It reaches the exact test ceiling
without growing output backlog: 1.770 s maximum arrival-to-commit, 230 ms margin
to the producer period, output high-water one. Twenty-two maximum frames commit;
the next reaches the finite fence and is accounted as lost/full. All 931 program
operations stay in `0x002000–0x03FFFF`, at most once per page. This is simulation,
not measured worst-case physical latency for the installed chip.
This is a core-engine fence stress test deliberately run beyond 30 seconds
without the session frontend; the actual profile's frontend still stops capture
at 30 seconds. It does not predict 23 blocks during a normal 30-second capture.

## Programming, data and failure contracts

Programming remains one aligned 256-byte PP per successful page, four 64-byte
wire slices under one CS, WREN/WEL checks, 2 ms BUSY polling, 1-second program
timeout and full readback/CRC32/inline-seal validation. The fixed 14-data-page
sector layout and protected diagnostic boundary do not change. No driver erase
function or status-register write has been introduced. Exact raw axes, shared
uint64 timestamps, sentence-scoped GPS presence/fix/UTC fields and compression
raw fallback remain unchanged. The host decoder needs no format changes.

Acquisition loss, source overflow, flash status/readback/CRC failure, finalisation
deadline or full test window stops capture and produces explicit loss/fault
evidence. A flash/finalisation fault locks further writes, without an automatic
footer claiming success. A source overflow can drain to a loss-marked footer.
On reset/brownout the firmware does not auto-record; the future host runner must
abort on reset evidence and never issue another START. No physical power-cut
experiment is part of this phase or the proposed next trial.

## Simulation evidence

The immutable seed `tests/tcb3/fixtures/phase3b7-session.bin` is copied from the
actual Phase 3B.7 recovered image (SHA256
`8bf910507bac630695a28b2b7c42a9a34d0d06ebda9a30016cc82d0a898dc1bc`).
It contains 500 IMU/50 RMC/5 GGA records and their existing CRCs/footer. Tests
compare its bytes exactly after every normal append and interruption recovery.

New production-ARM tests cover:

* USB-disconnected autonomous 30-second capture, old session unchanged, exact
  original new typed bytes recovered, combined TPX3 export and each complete
  session decoded independently.
* Hidden payload candidate skip; full/occupied fence; missing preserved prefix;
  occupied sector beyond the fence; post-probe mutation; independent adapter
  refusal below the floor, at the ceiling and at the diagnostic sector.
* Torn-header and interrupted-data-page CPU reset models, recovery of earlier
  committed blocks, preservation of the entire interrupted image, and explicit
  new-session append after it. Expected CRC/interruption issues remain visible;
  they are not misclassified as clean sessions.
* 60-second watchdog with no foreground persistence slack, TIMER4 low-word
  rollover, denied WEL, and overload stopping with explicit footer loss.
* Additional 100 us injected cost per active compression unit with 10 ms program
  BUSY, no loss, and all original timestamps reconstructed exactly.
* Read-only probe cancellation and maximum-frame pacing to the exact write fence.

The normal high-entropy 30-second model captured **3000 IMU, 300 RMC, 30 GGA**,
15 frames, 225 programs and allocated 15 new sectors. It appended at `0x2000`
and ended at exclusive `0x011000`; the old two sectors remained byte-identical.
The combined export was 76,224 bytes. Model flash service maximum was 544 us and
block persistence 356 ms (357 ms with injected compression CPU load); input/output
high-water 2/1. These values exclude real Arduino/IRQ/electrical overhead.

The complete existing regression set, including the five-second test profile,
is retained. Exact commands and final results are saved under
`logs/phase3b8-review/`. No simulation uses a physical serial device or SPI flash.

## Build and proposed later hardware sequence

Final validation: **all 18 regression suites passed**. Commands and exit codes
are recorded in `logs/phase3b8-review/regressions.json`. The standalone decoder
also decoded the combined TPX3 example into 100,800 exact canonical record bytes,
with both sessions complete and no export/session issues. Example and decoder
output are saved alongside the regression logs.

| Build | Static RAM | Program storage |
| --- | ---: | ---: |
| Default, TCB3 disabled | 118,992 bytes | 165,336 bytes |
| `tcb3_append_30s` | 120,692 bytes | 181,928 bytes |

The enabled build's static RAM is unchanged from the five-second engineering
build. These are linker allocation figures, not physical stack/heap high-water
measurements. The default ELF SHA-256 remains
`a83cbaabf9c0e18034a5f70a987539c77c574e429e9b6aa3a06caf6d1a836089`.
The new append ELF SHA-256 is
`8b9999ea02f29be0140c98c0873420b68d5a7e27cbca815ffa1c9ba44a420c76`;
its ZIP SHA-256 is
`c415e67b2db67cfbc2beb0fe366f2106bfd40e870820031546485fbbca4bff6a`.
Additional artifact hashes are in `artifacts.json`.

Production changes are limited to `platformio.ini`, `src/tcb3_test_limits.h`,
`src/tcb3_store.h`, `src/tcb3_store.cpp`, `src/tcb3_session.cpp` and
`src/flash_write_test.cpp`. Tests add the three append ARM harnesses,
`tests/tcb3/append_30s_tests.py`, and the archived first-session fixture plus
provenance manifest. `tests/tcb3/integration.cpp` permits a test-specific NOR
model size. The V1 serializer, frozen V2 specification and host decoder are
unchanged. No upload or physical flash access was performed.

Build only; no upload target:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e tcb3_append_30s
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/tcb3/append_30s_tests.py
```

New artifact: `.pio/build/tcb3_append_30s/firmware.zip`. Do not substitute the
five-second or an unbounded enabled artifact. Review hashes and runtime marker.

After separate upload/write approval, keep stable USB power. Verify normal boot,
read-only recovery, GPS RAM profile, identity/SFDP/status, RECORD STOPPED,
OBS STOP/OFF, COMP STOP/OFF/empty and the 3B8 limits marker. Capture TCB READ before
starting and require the old decoded session and its two sector bytes to match
the Phase 3B.7 archive. There is no erase step.

Issue **TCB START once**. It scans/probes read-only and only then reports STARTED.
Expect session 3 at `0x2000`; record/refuse surprises before accepting the test.
Leave OBS diagnostic capture off; TCB owns the compressor and 100 Hz acquisition.
Observe TCB STATUS/COMP STATUS/GPS STATUS sparingly. Firmware auto-stops capture
at 30 seconds; **TCB STOP** remains an early-stop option. Require a clean STOPPED
footer before the 60-second total deadline and wait for the legacy read-only
append rescan. Do not reset, retry, erase or start another session on failure.

Capture **TCB READ** twice as exact raw TPX3 bytes through END, compare hashes,
then run `tools/tcb3_decode.py` on the host. Require all normal-run issue lists
empty, both complete sessions independent, original session bytes unchanged,
new offered=committed, zero loss/reject/missed/full/CRC/incomplete counters and
correct metadata/validity masks. Check approximately 3000/300/30 counts with
endpoint tolerance, per-stream timestamp spacing, advancing GPS UTC and no
fabricated values. Verify all newly recovered pages are above the old prefix
and below `0x40000`. Address guards/export evidence do not substitute for an
independent bus trace or full physical outside-window readback.

Keep physical recovery read-only if anything fails; preserve the old/new data and
review before further mutation. A future authorised recording can append after
an interrupted session, but no automatic recovery recording is scheduled. Leave
recorders stopped and release USB after the test. Long-duration, brownout,
temperature/endurance and full live stack/heap qualification remain outstanding.
