# Phase 3B.4 read-only flash qualification

2026-10-31. **Physical TCB3 recording remains NO-GO.** The saved observations
support the XTX XT25F128 family; they do not distinguish B/F or establish the
authenticity/revision of the chip marked W25Q128. No hardware access, firmware
upload, flash programming, status write or erase was performed in this phase.
TCB3 is still disabled in normal builds. The V1 writer, V2 specification,
TCB3 3.2.0 writer and partition were not changed.

## Datasheet evidence

Primary manufacturer-authored documents reviewed through the web tool:

| Document | Relevant evidence |
|---|---|
| [XTX XT25F128B Rev 1.2, 2020-03-20](https://datasheet.lcsc.com/lcsc/2005251034_XTX-XT25F128BSSIGT_C558844.pdf) (distributor mirror) | ID table: `9F -> 0B4018`, `90 -> 0B17`. Section 6.14: `02h`, WREN, three address bytes, page wrapping, CS low until byte-aligned completion, BUSY polling and WEL reset. Section 7.8: tPP typical 0.3 ms, maximum 0.75 ms at -40..85 C, 2.70..3.60 V. Section 6.37: 128-bit UID through `5A` at `0x000094`. Table 3: SFDP 1.0, NPH=1; BFPT nine DWORDs at `0x30`, vendor table three DWORDs at `0x60`. |
| [XTX XT25F128F Rev 1.2, 2023-05-16](https://cdn.semikey.com/upload/pdfs/7f/97/7f97109b0187d7aa9ee7c57f038dfe1c.pdf) (distributor mirror) | ID table: same `0B4018` and `0B17`. Sections 5.1.4/5.1.5: 256-byte SFDP register using `5A` plus 24-bit address/eight dummy clocks; UID through `4B` plus four dummy bytes. Section 5.2.10: same ordinary 1..256-byte `02h` page program protocol. Section 6.6: tPP typical 0.4 ms; maximum 2 ms (-40..85 C), 3 ms (-40..105 C). Exact SFDP contents require XTX's table. |

Both describe ordinary page programming; the reviewed sections do not establish
an arbitrary five-partial-program allowance. Neither provides a blanket
power-fail atomicity guarantee for this recorder. Timing values above are
candidate-document limits, not measured values or assigned installed-chip limits.

XTX's [official product page](https://www.xtxtech.com/en/Products/info.aspx?productModel=XT25F128FWOIGT)
lists both B-to-F migration guidance and AN10061 covering SFDP/UID/OTP differences
between process families. Its signed download links for those notes could not
be retrieved by the web tool, so their contents are not treated as verified.
Local datasheet PDFs were not archived. The B mirror also exposes an apparently
inconsistent density entry in its text-rendered SFDP table; do not fabricate a
complete expected SFDP image from that table or use it instead of hardware data.

## Comparison with saved hardware evidence

Evidence file: `.pio/phase23-hardware/startup-serial.txt`.

| Saved observation | Assessment |
|---|---|
| Five `0B4018` responses | Matches both candidate ID definitions; does not select B/F |
| `53 46 44 50 00 01 01 FF` | Matches B Table 3's signature/revision/two-parameter-header structure; F exact contents not verified |
| BFPT 1.0 | Consistent with B's table; raw parameter headers, addresses and lengths were not saved |
| 134217728 bits / 16777216 bytes | **128 Mbit / 16 MiB**; consistent with both parts' stated capacity |
| Reads `3B`, `BB`, `6B`, `EB`; erase descriptors `20/52/D8` | Compatible family capabilities, not revision fingerprints; no advertised erase command was executed |
| Missing raw SFDP, status and secondary ID evidence | Cannot compare vendor table, protection state, UID location or complete parameter fingerprint |
| Breakout marked W25Q128 | Conflicts with electronic family evidence; label alone cannot resolve manufacturer/die provenance |

This is stronger support for an XTX family match than the breakout marking,
but identification is still provisional. A UID is a consistency/provenance aid,
not proof of authenticity or a documented revision identifier.

## Review of existing SPI reads

`main.cpp::readFlashJedecId`, `recorder.cpp::identity`, the diagnostic test
preflight and `tcb3_session.cpp::storeIdentity` all issue `9F` then clock three
bytes under one CS assertion, MSB first, mode 0 at 1 MHz. There is no extra
address/dummy byte that would shift that response. CS is deasserted before
ending each transaction. Five repetitions agree in the saved log.

`main.cpp::readSfdp` sends `5A`, three address bytes MSB first, exactly one dummy
byte, then reads data without toggling CS. It decodes returned DWORDs little
endian. Header NPH is interpreted as count minus one, BFPT selection is bounded,
and density units are correctly converted. No transaction-framing defect was
found in this source review. It cannot replace a logic-analyzer check of wiring,
voltage levels, CS/SCLK edges or actual core timing.

The old diagnostic only prints nine BFPT DWORDs' interpreted fields. It does
not preserve parameter-header bytes, vendor tables or programming timing data;
therefore its log cannot qualify the new writer. Stable all-zero/all-FF responses
can be electrically consistent without being valid identities. The new capture
always leaves qualification UNRESOLVED rather than automatically whitelisting a
part or changing flash configuration.

## Prepared read-only command: DIAG ID

This command is implemented for a later, separately authorized firmware upload.
It is **not available on the device merely because this source was built**.
It never runs at startup. Existing `DIAG` remains available; its family label now
mentions both B/F. `DIAG ID` requires flash/recorders/GPS diagnostics idle and
OBS/COMP acquisition stopped. It cancels on disconnect or an ownership change;
it does not pause/start/stop a recorder to obtain the bus.

Fixed transaction sequence:

1. `05`: read SR1. If unavailable, WIP=1 or WEL=1, stop. Do not clear WEL.
2. Five independent `9F` reads, three result bytes each.
3. `35`: read SR2 without changing protection, QE or other bits.
4. `90 00 00 00`: read two manufacturer/device-ID bytes.
5. Two passes of `5A` over addresses `0x00..0xFF`, in 16-byte chunks, with one
   dummy byte on every transaction. This includes the B UID location as raw
   evidence, without assuming that location means UID on every revision.
6. `05`: read final SR1 and stop on unavailable/BUSY/WEL. Report raw data and
   repetition comparisons, not automatic qualification.

There are 41 SPI transactions on success, at most 21 wire bytes per transaction.
One runs per foreground update. All output is deferred until capture completes,
then sent one short line at a time only when USB has sufficient buffer room.
Markers `BEGIN_FLASH_ID` and `END_FLASH_ID` bound the successful evidence report;
`FLASH ID ERROR` means capture was aborted and no complete report is available.
There is no unbounded table-pointer traversal: all 256 bytes are retained twice
regardless of parameter-header validity. Tables advertised outside that window
must be reviewed separately; they are not silently fetched.

The adapter allowlist is **05, 35, 90, 9F, 5A only**. It does not read the recording
array or diagnostic sector, and contains no program/erase/status-write path.
It sends no WREN/WRDI, reset, QPI, suspend/resume, deep-power-down/release, lock,
OTP or configuration instruction. In particular `AB` is avoided because it can
change power state. `4B` is deferred until the applicable family revision is
confirmed; the common B/F evidence capture does not need it. No pin wiring or
flash configuration is changed.

SR2 is raw evidence. Its interpretation must follow the selected datasheet,
including suspend/protection bits; this diagnostic is not a protection decoder
or a certificate that all sectors are writable.

## Compatibility review of the unchanged TCB3 3.2.0 writer

| Writer behavior | Review |
|---|---|
| `02`, aligned 24-bit address, exactly 256 bytes, one CS-low sequence | Fits the documented ordinary page-program protocol of both candidates; does not require a repeated-partial-program allowance |
| Four 64-byte transfer slices under one CS | One programming operation, with SCLK gaps; exact-part tolerance/real waveform still needs qualification |
| Per-page blank check and no second programming pass | Avoids reprogramming that page between erases; cannot establish the physical chip's entire prior erase history |
| WREN, WEL confirmation, WIP polling | Protocol agrees with the candidate documents; current conservative SR1==0 preflight refuses nonzero protection/status values rather than unlocking |
| BUSY polled every 2 ms; 1-second timeout | Long enough for both candidate tPP limits; timeout is a fault limit, not a promise of two-second throughput under pathological delays |
| WEL must clear; full-page readback and CRC/seal verification | Detects rejection/mismatch before releasing RAM block; does not depend on observing a BUSY pulse shorter than polling interval |
| No SR2/SR3 qualification in writer preflight | Suspend/protection-state handling needs exact-part review before enabling; writer does not issue suspend/resume or attempt to clear such states |
| CRC32 plus inline seal in same program | Integrity predicate, **not a temporally separate commit or atomic operation** |

Thus **the basic one-program protocol is compatible with the documented B/F
family command contract**, conditional on actual chip identification and status,
clock/power requirements. This is not full physical qualification. No need was
found to change the writer in this read-only phase.

After power interruption the unchanged recovery engine only releases complete
CRC-valid frames with valid page/session/sequence/fragment evidence. A torn or
occupied sector is skipped conservatively; there is no automatic resume,
overwrite or retry into a previously programmed page. A fully valid unacknowledged
page can recover. In-flight RAM and incomplete blocks can be lost, and CRC32 has
residual undetected-error risk. No electrical evidence yet proves that a brownout
cannot disturb other cells. Whole-block recovery tests are host models, not
power-cut qualification of this unidentified physical device.

Recording addresses remain `0x000000..0xFFEFFF`; `0xFFF000..0xFFFFFF` remains
protected diagnostic storage. The new diagnostic accesses identification/SFDP
register space only, not either array region. Existing V1/TCB3 writers and all
erase behavior are byte-for-byte unchanged from Phase 3B.3. No erase operation
was introduced and no physical recordings were accessed.

## Validation and next hardware test

The new suite has 13 scenarios exercising actual command dispatch, capture engine,
adapter and output handling: complete raw capture, correct dummy/address bytes,
BUSY/WEL/unavailable status, slow USB, OBS cancellation/refusal, disconnect,
unstable and malformed SFDP, forbidden commands/addresses, refusal during V1
recording and silence during V1 CSV export. Simulated array
canaries and write-enable/program/erase counters remain unchanged. Evidence in
`.pio/phase3b4-identity-example.txt` is explicitly a **simulation**, not a device
identification result. Build/regression results are recorded alongside it;
durable evidence is in `benchmarks/flash-identification/`.

The final normal firmware build and all 15 regression suites pass, including
the 13 new identity scenarios, existing V1/GPS/decoder tests, full-capacity paced
flash simulation and the concurrent 120-second IMU/GPS/compressor/store run.
These are host results; no physical identification result was obtained.

Normal-build static RAM is 118,992 bytes, +552 bytes for the fixed snapshot/state;
program storage is 165,336 bytes for the final normal build.
There is no dynamic allocation. The diagnostic was not timed on hardware.

Recommended next test, **after separate upload authorization**: upload this
normal build with TCB3 disabled, wait for startup scan and GPS configuration,
verify RECORD STOPPED and OBS/COMP stopped, then capture `DIAG ID` twice. Save
the complete reports and firmware hash. Passively observe CS/SCLK/MOSI/MISO if
available; verify `9F` and `5A` framing, no mutation opcode, stable raw SFDP and
unchanged status. Compare both parameter headers, BFPT and vendor table against
the exact-revision XTX reference/migration note; seek supplier/XTX confirmation
if the marking conflict persists. Do not issue recording, erase or destructive
diagnostic commands. Even successful ID reads do not qualify page programming
or brownout safety. **NO-GO for physical recording until those gaps are closed.**
