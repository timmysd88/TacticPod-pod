# Phase 3B.9 corrected 30-second physical append test — PASS

One authorised recording completed. No retry, erase, status-register write,
manual STOP, unrestricted recording or power interruption was performed.
COM8 was released. The two existing sessions remain on flash.

## Firmware and preflight

Uploaded only `logs/phase3b9-review/tcb3_append_30s-3b9.zip` using the established
application-only PlatformIO-packaged Adafruit nrfutil serial DFU procedure:

```text
adafruit-nrfutil.py dfu serial -pkg logs/phase3b9-review/tcb3_append_30s-3b9.zip -p COM7 -b 115200 --singlebank
```

Bootloader entry used the proven 1200-baud touch on the identified XIAO;
application USB returned on COM8. The DFU tool reported `Device programmed`.

| Artifact | SHA-256 |
| --- | --- |
| Uploaded ZIP | `8e17bab58b1d5b21fa6898c8d0e9b98902c7944f3c28b62455041a57dbe53f93` |
| Application payload in ZIP | `d6710beafe277697c1f32301661f601231974ba60a3273b992d3c21f9364227b` |

All 18 saved regression suites passed and every validated source/artifact hash
matched before upload. Runtime confirmed `profile=3B9 exact=2000`, 30-second
capture, 60-second total deadline, addresses 002000–03FFFF and V1 writes/all
erases disabled. Hashes verify the submitted payload; no independent firmware
readback hash was obtained from the target. No firmware source/config was changed.

Boot and USB communication were stable; no unexpected reset or disconnection was
observed. JEDEC was 0B4018 in all five reads, capacity 134217728 bits / 16777216
bytes / 16 MiB. Before and after recording, SR1 was 00 and SR2 02; both raw SFDP
passes matched the archived reference. No new protection/status anomaly occurred.
`DIAG ID` still prints `qualification=UNRESOLVED`: its exact-model/revision
qualification label is static, not a newly detected recording failure. The
measured fingerprint remains XT25F128B-compatible, not a proved model/revision.

GPS startup verified the existing 10Hz RAM profile without configuration writes:
MEAS 100ms, NAV 1, GPS/Galileo enabled, BeiDou disabled, RMC every epoch, GGA every
tenth epoch, other configured sentences off, UART 38400. No GPS test/restore,
BBR/flash/OTP writes or receiver power cycle was requested.

RECORD STOPPED, OBS OFF, COMP OFF/empty and TCB idle/zero programs were confirmed.
Read-only append recovery reported 0x2000. Three pre-start exports (two during
upload preflight, one immediately before START) matched the archived five-second
TPX3 export exactly: 9024 bytes, SHA-256
`e003fc70802374e1559c22cacc0930f5b1d58209da1868263e42126d87d5a972`.
It decoded as complete session 1 with 500 IMU / 50 RMC / 5 GGA, clean footer,
valid CRCs and unchanged 14400 canonical record bytes.

**The full proposed allocation was not independently pre-scanned.** The explicitly
approved evidence was the archived full-partition erase, known occupied first
two sectors, and the corrected firmware's full-sector checks. `TCB STARTED`
session 3 with `skip=0` establishes that the complete read-only 4096-byte initial
sector probe passed in the reviewed control flow. This is firmware evidence,
not a separate raw readback command. Full sector/page checks also preceded each
allocation/program, and the first operation was guarded again before WREN.

## Sole recording and integrity

`TCB START` was issued exactly once. It reported scanning, then STARTED state 2,
session 3, zero programs, no error, exact start 2000. Capture stopped automatically;
finalisation reported clean STOPPED state 1 with 15 blocks, 3330 offered and
committed observations, 132 page programs and no error. The host observed STOPPED
31.844 seconds after sending START, including the initial read-only scan and
finalisation. Metadata-start to capture-stop was 30.002275 seconds; metadata
precedes the firmware's capture-limit origin. The 60-second deadline did not fire.

| Stream | Recovered | Mean monotonic timestamp rate | Min / max spacing |
| --- | ---: | ---: | --- |
| Six-axis raw IMU | 3000 | 100.007226 Hz | 9267 / 10796 us |
| GPS RMC | 300 | 10.005064 Hz | 79942 / 118610 us |
| GPS GGA quality | 30 | 0.999863 Hz | 993849 / 1004533 us |
| EVENT | 0 | — | — |

IMU mean spacing was 9999.277 us, spacing standard deviation 416.625 us, maximum
absolute interval error 796 us relative to 10000 us. No fabricated catch-up
samples were inferred or added. RMC has 300 valid fixes and 300 unique advancing
UTC epochs, exactly 100 ms apart. GGA has 30 unique UTC epochs, exactly 1000 ms
apart, quality 2, seven satellites and HDOP 1.50. RMC masks 119/127 preserve
course availability differences; GGA mask 63 preserves available quality fields.
HDOP is not a measured accuracy in metres.

Every new record timestamp advances strictly, including cross-stream ordering;
no backwards/equal timestamps or event records occurred. Sessions have different
boot time origins: old timestamps must not be concatenated with the new session
as one continuous monotonic timeline.

Offered and committed counts both equal [3000,300,30,0]. Lost/rejected/missed/full,
page CRC failures and incomplete-write counters are all zero; footer reason is
clean. All page CRCs, commit seals, compression-block CRCs, page/block sequences,
metadata and footer accounting passed the host decoder. Block sequences are
0–14, sector sequences 0–8, data page sequences 0–121, footer sequence 122.

## Binary exports and independent decoding

| Capture | Bytes | SHA-256 |
| --- | ---: | --- |
| `append-30s-1.tpx3` | 49344 | `8e22bf5ad18f8a98c8d9ef7535603f6d882c32d2a0fc97362375204b1cef3240` |
| `append-30s-2.tpx3` | 49344 | `8e22bf5ad18f8a98c8d9ef7535603f6d882c32d2a0fc97362375204b1cef3240` |

Both raw binary captures are byte-identical; no text conversion was used.
TPX3 packet CRCs and sequence numbers passed. Each session was isolated into its
own TPX3 export with packet sequence/CRC reframing, decoded independently through
both the library and standalone `tools/tcb3_decode.py`, and compared against the
combined decoded wire payload. New canonical data is 86400 bytes. The extracted,
reframed `session-1.tpx3` is byte-identical to the entire archived export; the
actual first 8192 sector bytes are also identical, including blank pages.
There was no independent RAM/source tap during physical recording; exact source
reconstruction is supported by the validated lossless codec, frame CRCs and
matching decoder outputs, rather than a second sensor capture.

## Recovered flash address map

The new session allocated nine sectors (36864 bytes), **0x002000–0x00AFFF**.
There are 132 committed/programmed pages (33792 bytes): nine headers, 122 data
pages and one footer. Runtime program count matches the recovered page count.

| Sector | Header page | Data page range | Data sequence | Footer |
| --- | --- | --- | --- | --- |
| 002000 | 002000–0020FF | 002100–002EFF | 0–13 | unused/FF |
| 003000 | 003000–0030FF | 003100–003EFF | 14–27 | unused/FF |
| 004000 | 004000–0040FF | 004100–004EFF | 28–41 | unused/FF |
| 005000 | 005000–0050FF | 005100–005EFF | 42–55 | unused/FF |
| 006000 | 006000–0060FF | 006100–006EFF | 56–69 | unused/FF |
| 007000 | 007000–0070FF | 007100–007EFF | 70–83 | unused/FF |
| 008000 | 008000–0080FF | 008100–008EFF | 84–97 | unused/FF |
| 009000 | 009000–0090FF | 009100–009EFF | 98–111 | unused/FF |
| 00A000 | 00A000–00A0FF | 00A100–00AAFF | 112–121 | 00AF00–00AFFF |

All addresses are hexadecimal. Pages 00AB00–00AEFF remain FF. The next recovered
append position is **0x00B000**. The old range 000000–001FFF remains unchanged;
all new recovered pages are within the engineering fence 002000–03FFFF.
`new-session-pages.csv` lists every exact page address, magic and sequence.

This map is recovered page/readback evidence reconciled with the firmware program
counter. No independent SPI logic-analyser trace or full outside-fence/diagnostic
sector byte comparison was obtained. The diagnostic sector was excluded by the
reviewed boundaries and was not targeted; its physical contents were not read
independently in this test.

## Timing, queues and remaining qualification

| Measurement | Observed maximum |
| --- | ---: |
| Flash store service call | 1189 us |
| Block persistence (store metric) | 393 ms |
| Compression CPU work per completed block | 54137 us |
| Compression processing slice | 247 us |
| Input / output queue high-water | 2/2 / 1/2 slots |
| Backpressure slices / raw fallbacks | 0 / 0 |

The block CPU metric is accumulated work across slices, not one blocking call.
The store persistence metric is not the chip's individual page-program BUSY time.
No growing backlog, compressor fault, rejection, missed deadline or recording
queue overflow was observed. OBS's old diagnostic overflow counters (RMC 191,
GGA 14) remained unchanged from startup: with OBS OFF, its diagnostic queues are
bypassed after forwarding observations to the compressor. They are not losses
from this recording and were not cleared to hide them. Parser checksum/malformed/
overlong/partial counters remained zero.

These measured maxima apply to this workload, not universal worst-case bounds.
Static RAM remains 120692 bytes; live stack/heap high-water is not instrumented
by these commands. No electrical, brownout, endurance or broad environmental
qualification was attempted.

## Decision and final state

**GO to prepare a separately reviewed, bounded two-minute append test.** This
does not authorise or execute another recording. A new profile must preserve both
sessions and explicitly start at the newly verified append address with suitable
duration/address limits. The installed exact-2000 single-capture profile is used
and will refuse another session; do not reset or repurpose it for a longer run.

Final state: RECORD STOPPED, OBS OFF, COMP OFF/fault=0/queues empty, TCB idle and
finalised. GPS remains 10Hz verified RAM. COM8 is released. Existing contents are
preserved with no erase, retry, further recording or power-cut experiment.

Detailed evidence: `summary.json`, `analysis.json`, `decoded.json`, independent
session JSON/TPX3/wire files, standalone CLI results, startup/upload logs and
complete preflight/record transcripts. SHA-256 evidence hashes are in summary.json.
