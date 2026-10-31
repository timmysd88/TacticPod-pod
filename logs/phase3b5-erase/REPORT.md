# Controlled recording-partition erase: completed

The single authorized erase completed in **478.157 seconds (7 minutes 58 seconds)**.
No repeat, firmware upload, recording start or TCB3 enable was performed.

* Exactly one RECORD ERASE / RECORD ERASE CONFIRM pair was sent.
* Preflight confirmed 0B4018, 16 MiB, unchanged SFDP, SR1=00/SR2=02,
  RECORD STOPPED, OBS OFF, COMP OFF/empty and TCB DISABLED.
* Firmware reached the completion condition of its reviewed 4095-sector loop:
  first sector 0x000000, last sector 0xFFE000, exclusive end 0xFFF000.
* All **16,773,120 recording bytes** passed the firmware's all-FF readback,
  performed sector by sector before advancing. This is firmware readback
  verification, not a separate host dump of the full partition.
* No sector timeout, protection fault or readback failure was reported.
* The protected diagnostic sector 0xFFF000?0xFFFFFF was excluded by the reviewed
  address guards. No command targeted it. Its bytes could not be independently
  compared before/after because the installed firmware has no raw readback command
  for that sector; unchanged DIAG ID results are chip-register evidence, not a
  diagnostic-sector content hash.
* Final RECORD STATUS: STOPPED, next_sector=0x0, buffered=0, dropped=0, missed=0.
* Final OBS OFF; COMP OFF, fault=0, queues empty; TCB3 DISABLED.
* Before/after DIAG ID reports were identical. COM8 was closed and released.

The serial interface exposes ERASING and final completion, not an individual
hardware sector counter. The completed count follows from the reviewed loop
and its explicit success branch, not an independent SPI-bus trace.

Evidence: [result.json](result.json), [full transcript](erase-transcript.txt),
[preflight](preflight.json), [one-shot guard](attempted.json).

Physical TCB3 recording remains disabled. No additional erase or recording is
scheduled. Historical OBS overflow counters are retained from passive RAM capture
before this procedure and were unchanged; they are not erase failures.
