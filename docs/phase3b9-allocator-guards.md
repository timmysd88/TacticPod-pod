# Phase 3B.9 — exact-start engineering allocator

This review supersedes Phase 3B.8's skip-and-continue allocation policy for the
dedicated `tcb3_append_30s` build only. No hardware access, upload, recording,
external flash programming or erase is authorised or performed in this phase.
The installed hardware still has the earlier build; it has not received these
corrections.

## Scope and exact guards

The environment and `TACTICPOD_TCB3_TEST_BUILD=2` selection are unchanged.
The new runtime identity is
`test=30s,256KiB,60s profile=3B9 exact=2000`.
Normal TCB3-disabled builds and the five-second engineering profile retain their
existing behaviour. V1/V2 formats, TCB3 3.2.0 serialization, TPX3 and CSV export
formats remain unchanged.

The only allowed initial address is **0x002000**, session ID **3**, sector
sequence **0**. Physical programs remain restricted to **0x002000–0x03FFFF**;
the last permitted page begins at 0x03FF00. The recording partition still ends
before 0xFFF000. No erase API is introduced, all test-build erases remain blocked,
and the protected diagnostic sector is excluded. Capture stops at 30 seconds;
the hard finalisation deadline remains 60 seconds measured from capture start.
Both are enforced without USB or foreground flash-service permission.

The initial scan checks status and reads the two preserved sector headers in
full. Both must have valid CRC32 and commit seal, TPH3 magic, container 3.2.0,
session 1, sector sequences 0/1, expected payload size/flags and matching session
metadata. Header ambiguity, missing headers, read failure or unexpected occupied
header prefix anywhere after the preserved two sectors faults without programming.
This profile deliberately refuses recovery to any later starting address.

After that scan, every byte of 0x002000–0x002FFF is read in 64-byte slices with
status checks. **Any non-FF byte faults; no skipping occurs.** The initial
`Store::start` requires exactly 0x002000. Before its header, the entire sector is
checked again. Before every page program, all 256 destination bytes are checked.
Immediately before the first page-program dispatch, the store rechecks address
0x002000, current sector 0x002000, session 3, sector sequence 0 and next sector
0x003000. The physical SPI adapter independently checks the same first-operation
identity/address before WREN. Existing range/alignment/status checks still apply
to every program.

Every subsequent sector is checked in full immediately before its header is
scheduled. Nonblank bytes or read errors fault; the writer does not jump past
the sector. Existing 1-second program timeout, 2-ms busy polling, WEL checks,
single 256-byte page program, complete readback and CRC/seal validation remain.
Unexpected status, programming failures, overflow or missed acquisition deadlines
retain existing stop/fault behaviour. A fault never schedules erase or retry.

The capture latch refuses another START while pending, active or already used
in the same boot. After reset, any newly occupied sector from the trial causes
this exact-start profile to refuse another session. It never records at boot.
Interrupted blocks remain exportable as interrupted evidence; this engineering
profile does not start a new session after them. General recorder/decoder recovery
support remains covered by the non-profile-2 regression suites.

## Verification limits

The full proposed allocation is **not independently pre-scanned**. The approved
procedure relies on the archived full-partition erase, the known preserved
two-sector recording, and complete sector checks immediately before allocation.
The header scan is not a full verification of every byte of the old session or
unallocated sectors. A pre-start host TPX3 export must still match the archived
five-second recording, decode without issues and contain exactly 500/50/5 records.
The firmware checks old header integrity, not the old recording's entire payload
and footer against an embedded archive hash.

Host simulations cannot establish flash electrical timing, SPI bus integrity,
brownout behaviour or live MCU scheduling bounds. No power-cut test is proposed.
Independent physical write traces/outside-window readback are not supplied by
the existing command interface. Simulator program-address traces and runtime
range checks are the available address evidence until separately instrumented.

## Simulation coverage

The append suite uses the exact archived 8192-byte physical first-session image.
It verifies normal disconnected 30-second capture, lossless independent old/new
session decode, preservation of every old byte, timestamps and footer counts.
Additional cases cover:

- Hidden starting-sector payload, corrupt header prefix and a partial program
  at the last byte: all fault without WREN or programming.
- An unexpected later occupied header, an occupied sector beyond the fence,
  missing/corrupt predecessor headers, and post-probe mutation.
- Pending/active/completed repeated START, tampered start address, tampered
  first-program identity/address, and independent adapter refusal.
- Unreadable recovery scan, initial probe and allocated-sector checks.
- Interrupted headers/data, reset and refusal to append again, while retaining
  the original session and already recoverable blocks without any new writes.
- Hidden payload in a continuation sector: fault before that sector's header,
  preserving all its bytes and the original session.
- TIMER4 rollover, finalisation watchdog, denied WEL, overload stop/loss counters,
  100-us injected compression-unit cost and 10-ms flash busy periods.
- Maximum-size raw frames to the exact address fence, one program per page,
  diagnostic canary preservation and no writes below the initial floor.

The maximum-frame fence exercise uses the store directly beyond 30 seconds to
test the address fence independently. It cannot bypass the frontend capture limit
in the physical firmware.

## Review and next hardware test

**All 18 regression suites passed. Conditional GO for one separately authorised,
supervised 30-second hardware test using this corrected artifact.** Exact suite
commands and exit codes are in `logs/phase3b9-review/regressions.json`. The physical
test remains unperformed; the earlier installed artifact is not qualified under
the strict new abort conditions.

The normal model recovered 3,001 IMU / 300 RMC / 30 GGA observations (the IMU
endpoint phase permits 3,000 or 3,001), 15 blocks and 225 page programs, with
zero losses and an exact byte-for-byte reconstruction of the canonical source.
The old 500/50/5 session remains byte-identical. Both complete sessions decode
independently; standalone host CLI decoding also reconstructs the combined
100,824 canonical bytes exactly from a 76,224-byte TPX3 export.
The new allocation is 0x002000–0x010FFF in this model, inside the unchanged fence.
The conservative Phase 3B.8 allocation budget remains exclusive 0x02F000.

Model maximum flash-service time is 544 us; maximum block persistence is 356 ms,
or 357 ms with the injected compression CPU cost. Input/output high-water marks
are 2/1. Maximum-size frame stress with 400-ms service blackouts and 10-ms program
BUSY reaches 1,770 ms arrival-to-commit with queue high-water 1 and no growing
backlog. These are conditional simulation measurements, not physical WCET.

The default build uses 118,992 bytes static RAM and 165,336 bytes program storage.
The engineering build uses **120,692 bytes static RAM** and **182,768 bytes program
storage** (237,568 / 811,008 bytes available). RAM is unchanged from the previous
engineering build; checks reuse the existing page, metadata and scratch buffers.
These are linker figures, not physical stack/heap high-water measurements.
The default ELF is byte-identical to the Phase 3B.8 baseline, SHA-256
`a83cbaabf9c0e18034a5f70a987539c77c574e429e9b6aa3a06caf6d1a836089`.

New engineering artifact SHA-256 values:

| Artifact | SHA-256 |
| --- | --- |
| ELF | `8fd835ac50c608b699893a34310e358b397987e0ba3adb04cb4ffa3b16c7761e` |
| ZIP | `8e17bab58b1d5b21fa6898c8d0e9b98902c7944f3c28b62455041a57dbe53f93` |
| Application binary in ZIP | `d6710beafe277697c1f32301661f601231974ba60a3273b992d3c21f9364227b` |

Exact commands, build output, regression results and full artifact hashes are in
`logs/phase3b9-review/`. The ZIP is
`.pio/build/tcb3_append_30s/firmware.zip`; the older installed ZIP hash does not
identify this corrected build.

Production changes are limited to `src/tcb3_test_limits.h`,
`src/tcb3_store.cpp`, `src/tcb3_session.cpp` and `src/flash_write_test.cpp`, with
conditions restricted to test profile 2. No new production allocation was added.
Tests change `tests/tcb3/append_30s.cpp`, `tests/tcb3/append_store.cpp` and
`tests/tcb3/append_30s_tests.py`. `platformio.ini`, the frozen V2 specification,
V1 serializer and host decoder remain unchanged.

Conditional GO requires successful regression/build evidence listed in the
accompanying review logs. No upload or physical test follows automatically.
After separate approval, upload only the new hashed artifact, require its 3B9
marker, confirm JEDEC 0B4018 / 16 MiB / expected status and GPS profile, keep V1,
OBS and COMP stopped and validate the old export against its archive.

Issue `TCB START` once. Only session 3 at 0x002000 with zero skipped candidates
is expected. Let the automatic 30-second stop run, require clean finalisation
inside the 60-second deadline, then capture `TCB READ` twice as raw TPX3 bytes.
Decode both sessions independently; verify unchanged old page bytes, exact new
counts/timestamps/validity, CRCs, sequences and clean footer accounting. Any fault,
reset, loss, mismatch, timeout or incomplete finalisation ends the test: preserve
contents, no restart or erase, release COM8 and review the evidence.
