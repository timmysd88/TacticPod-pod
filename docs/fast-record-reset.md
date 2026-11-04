# Fast recording reset (`RECORD RESET`)

Reusable builds only. Clears all visible recordings in seconds without erasing the 16 MiB flash.
`RECORD ERASE` remains the separate full physical erase (default environment only).

## Command

```
RECORD RESET            arms a 30 s confirmation window and prints what will be hidden
RECORD RESET CONFIRM    runs the reset
```

Refused while recording, scanning, exporting, GPS-busy, or if JEDEC `0B 40 18` / status checks fail.
It never starts a recording.

## Format addition

* Sector 0 holds a 256-byte reset marker: `TPR3`, epoch (u32), CRC at 248, commit word `0xc0dec0de` at 252.
* Sessions written while a marker is active carry the epoch in the TCB3 header page at offset 120
  (metadata offset 104, previously reserved `0xFF`). Sessions without a marker are byte-identical to before.
* Epochs are never 0 or `0xFFFFFFFF`. A new epoch is `max(any epoch seen in a header or marker) + 1`.
* Block/record format is unchanged. `tools/tcb3_decode.py` accepts the epoch word and reports `reset_epoch`.

## Reset sequence and power-loss states

1. Scan every header for the highest epoch.
2. If sector 0 is blank, skip to step 5. Otherwise zero the old magic (legal NOR program) and verify.
3. Erase sector 0 and verify blank.
4. (merged with 3)
5. Program the marker and verify readback.

A power cut before step 2 leaves the old recordings intact; after it, scans and exports **fail closed**
(`RECORD READ`/`TCB READ` show nothing, recording refuses). Repeating `RECORD RESET` always recovers.

## Allocation after reset

* With a marker the append floor is sector 1 (0x1000); live data is the contiguous run of current-epoch headers.
* Stale sectors are erased lazily, one sector at a time, immediately before being written (blank-verified after).
  Torn erases leave a non-blank head that is re-erased on the next boot.
* The final reserved sector (0xFFF000) is never programmed or erased.

## Fresh / fully erased flash

Without a marker, the reusable firmware keeps its protected lower boundary `WriteStart=0x39000`.
A fully erased chip has no history, so allocation begins at exactly `0x39000` (previously the scan left
`next=0`, which failed with "reusable append boundary" and the three-red-flash fault). Candidate sectors are
blank-checked before writing. Blank sector 0 with data below `0x39000` is treated as an interrupted reset and fails closed.
`RECORD RESET` on fresh flash writes only the marker and then records from sector 1.

## Diagnostics

* Status lines are now streamed in the USB room available instead of waiting for the whole line to fit, so a
  long fault report can no longer wedge the console.
* `DIAG ID` is blocked by genuinely busy flash (pending/recording/exporting/resetting/open page). After a fault
  or failed reset it additionally requires the chip to report idle (no busy, no WEL) before reading the identity.
* A text status line that is only partly sent keeps ownership of the line buffer: new status text is dropped and
  `TCB READ` binary packets start only after the line's newline, so exports are never spliced with text.

## Integrity rules added after review

* The live-sector walk requires a sealed header page (CRC and commit word), not just magic plus epoch. An unsealed
  header with blank data behind it is a torn head and is reused; with non-blank data behind it the scan fails closed.
* Sector 0 counts as fresh only if the whole 256-byte page is blank. Blank first bytes with later programmed bytes
  is a torn marker and fails closed ("repeat RECORD RESET") in the scan, in `TCB READ` and in `RECORD READ`.
* The lazy erase hook only accepts the exact head sector (`stats.next`) while the store is probing or preparing it.

## 0x39000 protection policy and rollback

* Unreset flash keeps the protected lower boundary 0x39000 (fresh flash starts at 0x39000).
* After `RECORD RESET` the append floor is sector 1 (0x1000): the marker proves everything older was intentionally
  discarded, so the low region is reused.
* Do not flash older firmware onto a reset device. It does not understand the marker or epochs and can treat old,
  still-physically-present sessions as live again. Run the full `ERASE` maintenance operation first if you must roll back.

## Timing and limitations (not yet measured on hardware)

* Marker work is under 0.5 s; the epoch scan reads ~4095 sector headers, simulated at ~4.2 s at 1 ms/tick; real time is expected to be about 1-5 s.
* Old data stays physically present until reused (not a secure erase).
* First lap after a reset can stall about 45-400 ms per sector for lazy erases. The erase is polled, not blocking, and the
  compressor holds two input and two output buffers (about 9 s at the raw worst case of ~3.7 KB/s). Existing simulation
  uses a fixed 20 ms erase with an unpaced producer, so it does not prove sample-loss safety: real erase latency versus
  100 Hz IMU / 10 Hz GPS, USB streaming on a real host, and power-cut behaviour must be measured on hardware.
* Session numbers are address-derived, so the first session after a reset is session 2.
* Scans and exports are slightly longer (extra sector-0 classification).
* Needs hardware validation before use.

## Tests

`tests/reusable/reset_tests.py` (in `run_all_tests.py`): reset, reboot, repeated reset, torn marker/revoke/erase,
power cut at 151 points, interrupted lazy erase, fresh flash, repeated recording, legacy seed unchanged.
