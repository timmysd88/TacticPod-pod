# Persistent TCB3 container 3.1 and TPX3 export 1

Historical 3.1 contract. New writes use separately versioned
[container 3.2 and its Phase 3B.3 safety review](phase3b3-flash-safety.md).
The 3.1 layouts and fixtures below remain supported unchanged.

2026-10-31, Phase 3B.1/3B.2. Implemented and simulated; **physical recording is
disabled**. This outer container does not change the frozen V1/V2 formats or the
[TCB3 block 3.0 contract](recorder-compact-v3.md). A complete stored frame remains
byte-identical to the RAM compressor's output.

## Ownership, commands and activation

`TACTICPOD_TCB3_FLASH_ENABLED` defaults to 0 in tcb3_session.h and is not enabled
in platformio.ini. Normal firmware recognises TCB commands but reports DISABLED.
Only host tests enable it. An explicitly enabled compile was also built for RAM
sizing; it is not uploaded and is not the normal build artifact. Changing the
gate does not start recording automatically. No serial/device tools are used by
the tests or host decoders.

| Enabled-build command | Meaning |
|---|---|
| TCB START | Require V1 idle, IMU available, GPS diagnostic ownership free, exact JEDEC 0B4018, and no retained COMP RAM. Scan read-only, then append a new session |
| TCB STOP | Stop capture, seal the partial frame, drain retained frames, commit a footer |
| TCB STATUS | Report session/state, committed/offered records, rejects, missed intervals, full/CRC/incomplete-write counters and maximum service duration |
| TCB READ | Read-only TPX3 binary export of TCB sectors, while all recording/flash activity is idle |

TCB owns the compressor during capture and drain. COMP mutation, OBS reset/rate
commands, V1 mutations, GPS reconfiguration and flash diagnostics are refused
while it owns resources. STATUS commands remain available except during binary
export, when all incoming text commands are consumed without text responses.
USB disconnect aborts export. No erase command exists in the TCB interface or
storage transport. Existing explicitly confirmed legacy erases retain their
existing destructive meaning when idle; none are invoked by this implementation
or this validation. Do not use them to make recording space without separate
authorisation and backup.

OBS retention queues remain independent diagnostics: their overflow does not
discard a TCB observation, because the compressor receives it before OBS queue
admission. Use TCB/COMP rejects and footer accounting to assess recording loss.
Clients may send OBS STOP before TCB START to disable diagnostic queue retention;
TCB still requests 100Hz and receives fresh GPS observations. No OBS reset should
be sent during a TCB session.

STOP during the initial read-only scan cancels the pending start without writing
a header. A cancelled/incomplete scan cannot supply an append position; a later
START must scan again. Successful close also invalidates the scan lease so
portable callers cannot reuse a stale append cache after another recorder runs.

V1 acquisition/write/CRC/format/CSV logic is preserved. Its exporting() predicate
also mutes text during TPX3 export. After a TCB close, Recorder::begin performs a
new read-only legacy scan so its cached append position includes the new sectors.
TCB always scans again before START, including V1/V2/unknown/torn headers. Both
directions of V1/TCB append are tested. No old session is resumed after reset.

## Flash address and operation safety map

The chip is 128 Mbit = 16 MiB, not 128 MB. Addressing remains three-byte.

| Region | Inclusive addresses | Permitted TCB operations |
|---|---|---|
| Recording, 4095 sectors | 0x000000..0xFFEFFF | Bounded reads; append-only page programs into verified erased space |
| Diagnostic sector | 0xFFF000..0xFFFFFF | None: no reads, program or erase from TCB |

Each 4096-byte sector keeps page 0 as header, pages 1..14 as data, page 15 as an
optional final footer. Writes never cross a 256-byte page boundary. Every read
or program transport unit is at most 64 bytes. The SPI adapter uses the existing
D2 CS, D8 SCK, D9 MISO, D10 MOSI, mode 0, 1MHz and unchanged JEDEC whitelist.
The `0x03` reads issued while `TCB READ` is exporting use 4MHz
(`kExportReadHz`). By default these remain bounded 64-byte transfers across
cooperative export updates. Status, program, erase and all other reads stay at
1MHz.
There is no erase callback and no protection-register modification.

## Experimental bulk export status

The export engine has an optional `Port::readPage` provider callback for one
aligned 256-byte page. It is independent of the USB packet framing/output and
does not add a BLE download transport. When no provider is supplied, the engine
retains the existing 64-byte read path. USB backpressure is unchanged: packets
remain immutable until fully accepted by the USB writer, and export work is
serviced only when no packet is pending and foreground slack permits.

The nRF52 bulk SPI implementation is experimental and disabled by default
(`TACTICPOD_EXPORT_BULK=0`). Enabling it in a build is not a production
recommendation. The installed Seeed/Adafruit nRF52 SPI wrapper exposes
`transfer(tx, rx, count)` but returns `void`; its implementation calls
`nrfx_spim_xfer()` and discards the result. The installed nrfx SPIM implementation
requires EasyDMA buffers in Data RAM, returns errors for invalid addresses or a
busy instance, and performs a blocking event spin when initialized without an
event handler. Consequently this callback cannot report transfer failures,
uses a synchronous read that temporarily stops cooperative foreground work, and
its SRAM-address precheck does not provide equivalent nrfx error handling.
The baseline remains the default until a supported, error-reporting and
cooperatively serviced implementation is established.

Local PlatformIO build comparison against baseline commit `de278d5` (same
installed platform/toolchain; figures are linked ELF RAM and flash usage):

| Environment | Baseline RAM / flash | Candidate RAM / flash | Difference |
|---|---:|---:|---:|
| Default | 118,996 / 166,220 bytes | 118,996 / 166,220 bytes | 0 / 0 bytes |
| Reusable, bulk disabled (default) | 123,092 / 217,464 bytes | 123,108 / 217,480 bytes | +16 / +16 bytes |
| Reusable, bulk explicitly enabled | 123,092 / 217,464 bytes | 123,112 / 217,816 bytes | +20 / +352 bytes |

The explicit bulk environment uses `TACTICPOD_EXPORT_BULK=1`; it is not the
default. It was uploaded only for the approved read-only hardware benchmark.
These build sizes do not validate runtime safety. Relative to the candidate
reusable 64-byte fallback build, enabling bulk adds 4 bytes RAM and 336 bytes
flash. The bulk path must not support a production or BLE-compatibility claim.

Host validation: `python tests/reusable/run_all_tests.py` passed all 34/34
scripts on Windows, including the TCB3 suite's 38/38 tests, byte-identical
packet output between the sliced and page-provider models, corruption
detection, and no export writes. The V1/V2 fixture checks normalize JSON
line endings so generated LF content compares consistently with Windows CRLF
checkouts. The resource test passed after building the default ELF and measured
118,996 static RAM bytes.

## 4MHz export benchmark

On one TacticPod prototype, three consecutive exports of the same stored session
averaged 1.714 seconds at 4MHz, compared with a 4.704-second 1MHz baseline
average (about 63.6% less download time). Each capture was 138,944 bytes and
byte-identical to the verified baseline; packet CRCs, sequence numbers and
session completeness all validated, with no failures.

This benchmark covers one prototype only. The exact installed flash variant
and its rated maximum clock for the `0x03` read command remain unconfirmed, so
the result does not qualify 4MHz operation across other hardware units.

On 2026-11-09, the connected prototype's unchanged TCB3 session 2 was read
using baseline commit `de278d5` (the 4MHz/64-byte path). A verification export
and three timed exports each decoded as one complete 3.1 session: 11 sectors,
144 nonblank data pages, 3,542 records and 91,884 decoded raw bytes. Packet,
page, data-block and footer CRC/consistency checks passed; the footer reported
offered=committed `[3192,319,31,0]`, zero lost, rejected, CRC failures,
incomplete writes, full events or missed samples. Each timed capture was
49,344 bytes / 178 packets with the same SHA-256
`00a197013c16ab89252eb707fda56f67f753d59628c4c40822a4b2eb891c0d3a` and the
same decoded image and session-content hashes. Elapsed times were 0.6135,
0.6083 and 0.6079 seconds (mean 0.6099 seconds). Raw captures and the detailed
results are preserved under
`../logs/flash-export-benchmark-2026-11-09/`. This is a baseline-only result;
the separate bulk comparison below measures the effect of 256-byte reads at
the same 4MHz clock.

## 4MHz bulk export comparison

The opt-in bulk firmware was tested against the three preserved baseline
exports above, on the same connected prototype and unchanged session 2. Both
firmware variants used 4MHz export reads; the only export-path difference was
64-byte cooperative reads versus the experimental 256-byte synchronous read.
The three bulk exports took 0.2820, 0.2650 and 0.2810 seconds (mean 0.2760 s),
versus the baseline mean of 0.6099 s: 54.7% less elapsed time (2.21x).

Each bulk capture was 49,344 bytes / 178 packets with SHA-256
`00a197013c16ab89252eb707fda56f67f753d59628c4c40822a4b2eb891c0d3a`.
All three decoded flash images were byte-identical to the baseline image
(SHA-256 `5805beb439ba4051da027f2d785169f13710ab7bc5dd88b266786c121cbfaa56`).
Packet sequence/CRC and page/session decoding checks reported no issues. Each
contained the same complete session 2: 11 sectors, 144 nonblank data pages,
3,542 records and 91,884 raw bytes (raw SHA-256
`d2b898a7dabaa0b79d0b1cd6c56926322c584a1ce7b68ee569bdbfb7aa42a7ef`). The
captures and machine-readable measurements are preserved under
`../logs/flash-export-benchmark-2026-11-09/bulk-4mhz-candidate/`.

This is a three-export result on one unit and does not resolve the exact flash
part's 4MHz rating or the SPI wrapper's discarded-error/blocking limitations.
It demonstrates the measured bulk-transfer effect only for this device and
recording, not production safety or future BLE servicing compatibility.

## Page envelope

All integers are little-endian, no implicit padding. Unused bytes are FF.

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII TPH3, TPD3 or TPE3 |
| 4 | 4 | Session ID, first sector index + 1 |
| 8 | 4 | Global data-fragment page sequence, zero-based; headers/footers contain next expected sequence |
| 12 | 2 | Payload bytes used, <=232 |
| 14 | 2 | Flags, zero |
| 16 | 232 | Payload then FF padding |
| 248 | 4 | CRC32/IEEE over bytes 0..247 |
| 252 | 4 | Commit 0xC0DEC0DE, written last |

CRC is reflected 0xEDB88320, initial/final XOR FFFFFFFF, matching zlib.crc32.
The full destination sector is checked in 64-byte slices before its header.
The destination page is checked again before every page operation. CRC creation
is sliced with that check. Four 64-byte body programs include FF at 252..255;
each is polled and read back. Only then is the four-byte commit marker programmed,
followed by a full-page sliced readback and CRC/marker verification. A 2ms poll
interval and 1000ms program timeout are retained.
WIP, WEL, identity/address, readback or timeout uncertainty latches a fault and
locks all flash activity until reboot. A fault does not attempt to write a footer.
The adapter uses five partial page-program operations per page (four body chunks
plus the marker). The exact physical part's permitted partial-program count and
retention behaviour must be verified before enabling this adapter. JEDEC/density
alone do not establish that five cycles are permitted. The simulation includes
a four-cycle limit and confirms safe refusal of the fifth; it cannot prove a
real chip's retention limits. A different transport batching/DMA strategy may be
needed without changing the container bytes.

The sequential append scan reserves every nonblank sector-header prefix,
regardless of magic, version or validity. A blank prefix is only a candidate;
old payload below a blank header causes refusal, never overwrite. Conservative
recovery does not recycle holes. Session IDs are unique within this physical
recording area until an explicit external erase. A present device ID namespaces
them across pods; otherwise clients must keep the source image identity.

## Header payload, 160 bytes

All offsets below are absolute page offsets. Every continuation repeats the
same metadata except sector sequence; page offset 8 advances normally.

| Offset | Bytes | Meaning |
|---:|---:|---|
| 16 | 4 | Container version 0x00030100 (3.1.0) |
| 20 | 4 | Sector sequence within this session, starting at zero |
| 24 | 8 | Monotonic session start microseconds |
| 32 | 8 | Device ID, zero when unavailable; hardware adapter reads FICR when available |
| 40 | 4 | Clock Hz, 1000000 |
| 44 | 4 | Target IMU acquisition Hz, 100 |
| 48 | 4 | Sensor ODR Hz, 104 |
| 52 | 4 | Acceleration sensitivity micro-g/LSB, 244 |
| 56 | 4 | Gyro sensitivity micro-degrees/second/LSB, 35000 |
| 60 | 4 | Target RMC Hz, 10 |
| 64 | 4 | Target GGA Hz, 1 |
| 68 | 4 | GPS UART baud, 38400 |
| 72 | 4 | Nominal block window us, 2000000 |
| 76 | 4 | Maximum frame bytes, 8228 |
| 80 | 4 | Reconstructed typed-record schema, 1 |
| 84 | 4 | Availability: bit0 device ID present; bit1 build ID present; other bits zero |
| 88 | 32 | Build ID bytes; all FF and absent in this phase |
| 120..175 | 56 | Reserved FF |

Mandatory metadata fits in 160 of 232 payload bytes, with 72 bytes spare in
the header page. Rates are declared targets, not evidence of a valid GPS fix or
measured sensor cadence. UTC/fix/quality are the sentence-scoped record values.
No accuracy, UTC date, random boot UUID or unavailable build identity is invented.
Reboot starts a new monotonic domain; physical session identity distinguishes
domains even when timestamp origins repeat.

## Data-page fragment payload

| Offset | Bytes | Meaning |
|---:|---:|---|
| 16 | 4 | TCB3 block sequence within this session |
| 20 | 2 | Offset within the complete TCB3 frame |
| 22 | 2 | Complete frame length, 36..8228 |
| 24 | 2 | Fragment bytes, 1..216 |
| 26 | 2 | bit0 START iff offset=0; bit1 END iff offset+count=length; all other bits zero |
| 28 | 4 | TCB3 frame CRC (copy of the frame's final four bytes) |
| 32 | 1..216 | Exact frame bytes, including its header/CRC |

Payload-used is 16+fragment bytes. One frame starts on a fresh data page, spans
as many pages/sectors as needed and never shares a page with another frame. A
full 8228-byte fallback/stress frame needs 39 data pages. The fragment envelope
reduces usable frame bytes per data page from the earlier capacity assumption
of 232 to **216**; update capacity estimates accordingly. No claim of 300 minutes
on 16MiB follows from this implementation.

The two compressor output slots are the bounded flash queue. The writer leases
the oldest frame and releases it only after END-page commit verification. That
is also when per-type committed counters advance. No additional 8228-byte copy
is allocated. CRC validation of offered frames runs in 64-byte slices before
data programming. One service call performs a bounded read, program, poll or
state transition. Main-loop service follows acquisition and uses scheduled
slack; no WIP busy wait exists.

## Footer payload, 160 bytes

| Offset | Bytes | Meaning |
|---:|---:|---|
| 16 | 4 | Stop reason: 0 clean, 1 full, 2 overflow, 3 missed acquisition interval |
| 20 | 4 | Finalised flag, exactly 1 |
| 24 | 8 | Monotonic stop-request microseconds |
| 32 | 32 | Four u64 offered observation counts: IMU/RMC/GGA/EVENT |
| 64 | 32 | Four u64 fully committed-block record counts |
| 96 | 32 | Four u64 lost counts, offered minus fully committed |
| 128 | 4 | Rejected observation count |
| 132 | 4 | Detected CRC/readback failures |
| 136 | 4 | Detected incomplete/uncertain write failures |
| 140 | 4 | Flash-full detections |
| 144 | 4 | Missed IMU scheduling intervals |
| 148 | 4 | Fully committed blocks |
| 152 | 4 | Committed data-fragment pages |
| 156 | 4 | Reserved zero |
| 160..175 | 16 | Reserved FF |

Offered counts include the first rejected observation that causes capture to
stop, not hypothetical observations after stop. Missed intervals are counted
separately; missing samples are never fabricated. Faulted/unclean sessions have
no trusted footer and may have unknown RAM/power-loss counts. Do not infer zero
loss from an absent footer.

On overflow/deadline loss, capture stops and retained frames drain before a
loss-marked footer. On full, a partially persisted frame is not counted committed;
remaining offered records are counted lost and the reserved footer page records
the full stop. Remaining RAM is retained until explicit COMP CLEAR. Under normal
stop, all accepted records drain before finalisation.

## Recovery and corruption

After reset, scan read-only and start only in a fresh verified blank sector.
Never resume an old frame/session. Host recovery verifies page CRC/marker,
session/sector/page sequences, metadata continuity, fragment offsets/flags,
block CRC, typed-record validity and cross-block timestamp order. A torn/corrupt
fragment discards its entire partial frame. A later independent START can be
recovered, including after a missing continuation header; every gap is reported.
A valid footer is reconciled against the reconstructed counts. Unknown mandatory
versions, malformed flags, counter underflow and clean footers containing loss
are rejected. Recovery is not silent repair or a zero-loss power-fail guarantee.

## TPX3 binary export protocol 1

No ASCII preamble, CSV escaping or base64. Each packet is:

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII TPX3 |
| 4 | 1 | Export protocol version 1 |
| 5 | 1 | Kind: 1 BEGIN, 2 PAGE, 3 ISSUE, 4 END |
| 6 | 2 | Flags zero |
| 8 | 4 | Packet sequence, contiguous starting at zero |
| 12 | 4 | Flash address / recording end for END |
| 16 | 2 | Payload bytes, <=256 |
| 18 | 2 | Reserved zero |
| 20 | variable | Payload |
| 20+payload | 4 | CRC32/IEEE over header and payload |

BEGIN payload is four u32: FFF000 recording end, 256 page bytes, 4096 sector
bytes, 0x00030100 container version. PAGE payload is exactly one physical page.
Every sector with a TPH3 prefix is exported as its complete 16 pages, even when
its CRC/commit is torn. V1/V2 sectors are skipped; retain RECORD READ for the
existing V1 CSV. Unknown occupied header prefixes yield ISSUE with the 16-byte
prefix; read failure yields ISSUE with no payload and an interrupted transfer.
ISSUE is diagnostic, never silently treated as a valid empty session. END has
zero payload and address FFF000. The largest packet is 280 bytes.
Unexpected WIP/WEL, invalid status or read failure aborts export without a valid
END; a missing chip cannot be silently accepted as an empty successful transfer.

This is a **TCB-only sector export**, not a byte-for-byte backup of all flash.
The host's reconstructed sparse image fills unexported addresses with FF; that
does not assert those addresses were physically blank. Before/after V1/V2
preservation tests compare the actual simulated backing store, not this export.

USB may split packets arbitrarily. Packet offset advances only by the actual
successful write count. Zero TX space pauses with fixed RAM and performs no
further scan until the packet drains. Disconnect aborts; restart TCB READ from
the beginning. No resume token or random seek exists in v1. Clients stop at a
CRC-valid END; deferred text diagnostics may follow after the transfer ends.
Missing END, sequence gaps, trailing bytes and bad CRCs are errors in the strict
host decoder. Future Web Serial code must use an incremental binary parser and
BigInt for uint64; JSON exports represent all uint64 values as decimal strings.

## Host example

```powershell
python tools/tcb3_decode.py tests/tcb3/fixtures/example.tpx3
python tools/compact_decode.py --container tests/tcb3/fixtures/example.tpx3
python tools/tcb3_decode.py tests/tcb3/fixtures/example.tpx3 --raw-output .pio/new-decoded-v2wire.bin
```

Output is compared byte-for-byte with tests/tcb3/fixtures/expected.v2wire.
expected.json and manifest.json provide deterministic decoded output and hashes.
Both host tools read files only and refuse to overwrite --raw-output targets.
Neither opens serial ports. See [simulation results](phase3b-validation.md)
for the physical-writing NO-GO and remaining approval gate.
