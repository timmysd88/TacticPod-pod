# Phase 3B.9 hardware preflight — recording withheld

## Follow-up after conditional approval

The user approved archived erase evidence plus just-in-time full-sector checks,
but explicitly required abort on **any non-FF byte or address mismatch**, and
instructed stopping before START if any condition could not be established.
Review of the unchanged, hash-verified firmware found a further mismatch:
`Store::update()` in `State::Probe` increments `stats.next` and `probeSkipped`
when a candidate contains non-FF bytes, then searches the next sector. It does
not fault. The preceding header scan can also advance beyond expected 0x2000.
The existing guards require only the permitted address window, not equality to
the expected start address. Once the scan/probe completes, the session frontend
starts capture without a separate host approval gate. Inspecting STARTED/skip
and issuing STOP afterward cannot guarantee abort before programming.

The later `State::Blank` check does read every byte of the allocated 4096-byte
sector in 64-byte slices immediately before its header, and faults on non-FF.
Each destination page is independently blank-checked again before programming.
Those checks protect existing contents but do not supply the stricter requested
abort behavior during initial candidate selection. Consequently START remains
withheld. No firmware changes or uploads were made in this follow-up.

A fresh read-only preflight confirmed RECORD STOPPED, OBS OFF, COMP OFF/empty,
TCB idle/zero programs/error=none, recovered append 0x2000, expected profile,
JEDEC/SFDP/status, GPS RAM 10Hz profile, and the unchanged complete old session.
`strict-conditions-before.tpx3` is again exactly 9024 bytes with the archived
hash below and 500/50/5 records. All 18 suite results and reviewed source hashes
still match. COM8 was released. Evidence is in `strict-conditions-result.json`
and `strict-conditions-transcript.txt`.

**Current decision: NO-GO under the strict abort conditions.** A separately
reviewed build would need to fault on a non-FF initial candidate and enforce
the expected start address before starting acquisition or programming. No
such change was assumed authorised for this tested-artifact-only run. The
full proposed allocation was not independently pre-scanned. No recording
attempt, erase or retry occurred, and no new timing/write-map result exists.

## Original upload and read-only preflight evidence

The dedicated tested `tcb3_append_30s` application was uploaded using the established
application-only nrfutil ZIP procedure, bootloader COM7, application COM8. No
firmware source or build configuration was changed. All 18 saved regression
suites passed, and every saved source/artifact hash matched before upload.

ZIP SHA-256: `c415e67b2db67cfbc2beb0fe366f2106bfd40e870820031546485fbbca4bff6a`.
Application SHA-256: `1a65fbeb51c68b668f944fe98bc4ff2c5eae6612103acd0c1298de635fd8e7e7`.
DFU reported Device programmed. Runtime confirmed profile 3B8, 30-second capture,
60-second total deadline, write addresses 002000–03FFFF, V1 writes/all erases
disabled. The hash is verification of the submitted payload; there is no
independent target firmware readback hash.

Boot was stable, the read-only append scan returned 0x2000, and GPS reported
STARTUP 10HZ ALREADY VERIFIED, RAM, UART 38400, without configuration writes.
Five JEDEC reads were 0B4018, SFDP advertised 134217728 bits / 16777216 bytes,
SR1 before/after 00 and SR2 02. Both raw SFDP passes matched the archived reference.
No identity, protection-state or CRC fault was observed.

Two exact raw binary preflight exports were 9024 bytes, identical to each other
and to the archived Phase 3B.7 export:
`e003fc70802374e1559c22cacc0930f5b1d58209da1868263e42126d87d5a972`.
The decoder recovered the sole complete session at sectors 0 and 0x1000, with
500 IMU, 50 RMC and 5 GGA records, no issues, and its original clean footer.
The 14400 decoded canonical bytes also match the archived original exactly.
Since the TPX3 packets themselves are identical, all previously exported page
bytes, timestamps, sequence numbers, metadata and counters are preserved.

**TCB START was never sent. NO-GO for starting under the unchanged preconditions.**
The tested firmware has no command to freshly read back all proposed blank
allocation sectors before START. TCB READ scans header prefixes but exports only
occupied TCB3 sectors. START performs a complete candidate-sector read-only probe
before acquisition, then each further sector is fully checked before programming.
The prior 4095-sector verified erase and these just-in-time checks are useful
evidence, but do not fulfill a fresh entire-allocation readback before recording.
The user has been asked whether to accept that existing verification sequence or
stop to prepare a separately reviewed read-only verification facility. No
alternative was assumed approved.

Final states: RECORD STOPPED, OBS OFF/RAM_fast_imu OFF, COMP capturing=0 fault=0,
empty input/output, TCB idle state=1, no programs, errors or losses in its counters.
OBS retains diagnostic overflow counts from startup (RMC 191, GGA 14 at final
snapshot); those are not a new recording result. No observation reset or recording
was used to conceal these counts. The compressor was never started.

No external flash programming, erasing, status writes, new recording, automatic
retry or power-cut experiment was performed. No new write addresses or recording
timing measurements exist. COM8 was closed in the transport's finally block.

Host-side JSON summary serialization initially failed because decoder raw bytes
were included directly, after the successful checks and serial close. The summary
was reconstructed from saved transcripts and unchanged exports without repeating
the upload or device commands. Evidence: startup.txt, upload.txt,
preflight-transcript.txt, before-1.tpx3, before-2.tpx3, decoded-before.json,
old-session.v2wire, artifact-checks.json and summary.json.

There is no new GO recommendation for longer recording: the 30-second trial has
not yet occurred. Preserve the installed bounded build and existing flash data
pending resolution of the pre-start verification requirement.
