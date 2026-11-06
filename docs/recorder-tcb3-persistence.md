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
There is no erase callback and no protection-register modification.

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
