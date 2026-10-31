# TacticPod compact RAM block format 3.0

Phase 3A, 2026-10-30. This is a separate format; the frozen
[V2.0 specification](recorder-v2-format.md) is unchanged. Production emits
these blocks into bounded RAM buffers only. There is no compact flash writer,
partition change, serial binary export, or change to V1 RECORD commands.

## Block envelope

Integers are little-endian without alignment padding. Each block reconstructs
an ordered byte stream of the existing schema-1 V2 typed observations, including
their exact 64-bit microsecond timestamps. No timestamp, axis, validity bit,
GPS integer, EVENT detail, or record ordering is approximated.

| Offset | Bytes | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII `TCB3` (major format version 3) |
| 4 | 1 | Minor version, exactly 0 |
| 5 | 1 | Body predictor: 0 raw V2 wire records, or 8 adaptive columns |
| 6 | 1 | Dictionary/backend codec, exactly 0 (none) |
| 7 | 1 | Reserved, exactly 0 |
| 8 | 4 | Block sequence, starts at zero after explicit RAM CLEAR/START |
| 12 | 2 | Reconstructed V2 wire bytes, 1..8192 |
| 14 | 2 | Stored body bytes, 1..8192 |
| 16 | 2 | IMU_RAW count |
| 18 | 2 | GPS_RMC count |
| 20 | 2 | GPS_GGA_QUALITY count |
| 22 | 2 | EVENT count |
| 24 | 8 | Base device timestamp, first/minimum timestamp in this block |
| 32 | variable | Stored body |
| 32 + stored | 4 | CRC-32/IEEE over header and stored body, excluding this CRC |

The CRC uses reflected polynomial 0xEDB88320 and initial/final XOR 0xFFFFFFFF,
matching zlib.crc32. Total frame length is stored+36, at most 8228 bytes.
Record count is 1..256. Unknown versions, predictors, fields, mismatched lengths,
nonzero reserved fields, invalid reconstructed V2 records and CRC failures are
errors. Timestamps must be nondecreasing, below UINT64_MAX, and every timestamp
minus base must be less than 2,000,000 us. Equal timestamps are permitted.
No state or dictionary crosses a block boundary.

This sequence is local to the RAM pipeline, not a session ID. Sequence wraps
after roughly 272 years at nominal cadence; the host stream reader refuses
non-increasing sequences. A future writer must begin a new session before wrap.

## Predictor 8 body

First store the original record order: type IDs minus one, two bits each,
least-significant bits first, four records per byte. Unused high bits are zero.
The number of occurrences of each type must match the header counts.

Then, for each nonempty type in ascending order 1..4, store its timestamps,
followed by each of its payload columns in wire-field order. The field types,
scaling, masks, ranges and byte widths are exactly those of V2 schema 1:

| Type | Payload columns, in order |
|---|---|
| IMU_RAW | accel X/Y/Z and gyro X/Y/Z: six i16 |
| GPS_RMC | mask u32, latitude i32, longitude i32, speed u32, course u32, UTC date u32, UTC milliseconds-of-day u32, status u8, mode u8, talker u16 |
| GPS_GGA_QUALITY | mask u32, UTC date u32, UTC milliseconds-of-day u32, altitude i32, geoid i32, HDOP u16, satellites u16, quality u8, talker u16, reserved u8 |
| EVENT | code u16, severity u8, stream u8, argument u32, count u32, detail u64 |

The talker is treated as a little-endian numeric u16 solely for compression;
its reconstructed two bytes remain the original ASCII. Missing fields and
invalid fixes retain their masks and zero/missing representations. GGA quality
does not create accuracy values that were not received. U64 EVENT details are
always encoded as raw column mode 0; other columns use adaptive selection.

### Integer primitives

Unsigned varints use seven low bits per byte, low group first, bit 7 indicating
continuation. Encodings must be minimal and at most ten bytes. ZigZag maps
signed n to 2*n for nonnegative n and -2*n-1 otherwise. Predictor arithmetic
uses sufficiently wide integers and never wraps the original field width.

Bit streams pack low bits first. Each column ends at a byte boundary with
zero high padding. Residual widths may be zero (constant prediction).

### Timestamp stream

One mode byte precedes the stream. Each mode stores first timestamp offset from
base as a varint. Let gap[i]=offset[i]-offset[i-1].

| Mode | Remaining stream |
|---:|---|
| 0 | Each actual gap as unsigned varint |
| 1 | ZigZag(gap[i]-previous_gap) as varint, initial previous_gap=0 |
| 2 | First store positive gcd quantum as varint, then mode 1 using gap/quantum |
| 3 | First gap unsigned varint if present; subsequent gap changes as ZigZag(change)+1. A zero token followed by a positive run length repeats the previous gap that many times |

Mode 2 quantum is the gcd of actual gaps, or 1 when every gap is zero. Mode 3
run lengths cannot exceed remaining timestamps. Selection uses exact byte
cost, preferring the lowest mode on ties. This encodes measured timing,
including jitter, delays and rollover; it never substitutes an assumed 10ms.

### Payload column stream

Each column starts with a mode byte. Seeds use the original field's fixed
width, signedness and little-endian wire representation. A delta predictor has
one seed; delta-of-delta has min(count,2) seeds. Let residual be the current
value minus its previous value, additionally subtracting the previous delta
for delta-of-delta. Residuals are ZigZag unsigned integers.

| Mode | Representation after mode byte |
|---:|---|
| 0 | All values in their original fixed-width representation |
| 1 | One seed, then delta residual varints |
| 2 | Up to two seeds, then delta-of-delta residual varints |
| 3 | One seed, width u8, then fixed-width packed delta residuals |
| 4 | Up to two seeds, width u8, then packed delta-of-delta residuals |
| 5 | One seed, Rice k u8, escape width u8, then Rice delta residuals |
| 6 | Up to two seeds, Rice k u8, escape width u8, then Rice delta-of-delta residuals |

Packed/escape width is the bit length of the largest residual, zero if empty.
Decoder bounds width at 66; the embedded encoder's <=32-bit predicted fields
need at most 34. Rice k is 0..16, chosen by exact bit cost, smallest k on ties.
For unsigned residual r and q=r>>k: if q<31, emit q one bits, one zero bit,
then r's low k bits. Otherwise emit exactly 31 one bits followed by the whole
residual in escape-width bits, with no terminating zero. This limits unary
work. Reject overflowing reconstructed values and nonzero padding.

For each column choose minimum byte cost, including seeds, parameters and final
padding; prefer the lowest mode on ties. If the evolving adaptive body exceeds
the raw canonical wire size, discard it and copy the canonical bytes as
predictor 0. Thus stored body never exceeds raw size. The 36-byte frame overhead
still applies even to fallback blocks. There is no LZ dictionary or heap use.

## Framing, lifetime and recovery

An input block seals before admitting a record >=2 seconds after its first
timestamp, at 256 records, at the 8192-byte input limit, on STOP, or when the
foreground service notices two elapsed seconds. Empty blocks are not emitted.
Early/partial blocks are valid and reconstruct only samples actually captured.
At nominal rates a full block contains 200 IMU, 20 RMC and 2 GGA observations.

Two source slots and two immutable output slots bound RAM. The producer copies
each observation before any existing observation diagnostic queue can overflow.
Compression advances in foreground slices, after acquisition or with >=1ms
of scheduled slack, at most eight bounded work units and a 200us target per
call. A unit may finish after that target: it is not a hard real-time deadline.
Main-loop scheduling and the existing sensor/flash drivers are still subject
to interrupts, I/O execution and USB timing.

An unavailable consumer produces counted backpressure. When both source slots
are occupied, the first rejected observation increments rejection count and
latches a RAM compression fault, stopping further compression capture. Retained
sources and completed outputs remain intact. The counter records this first
rejection, not all sensor events after capture has stopped. V1 recording and
GPS reception continue independently. No automatic restart discards evidence.

The caller leases the oldest completed block with output() and acknowledges it
with release(). It must not retain the pointer after release() or CLEAR.
This phase deliberately has no automatic output sink, so a long COMP START
will reach backpressure; this is not a continuous compressed recorder yet.

The CRC and local predictor reset allow a known complete block following a
damaged block to decode independently. The strict host decode_stream fails on
truncation/corruption rather than silently skipping data. RAM is lost on reset.
A future flash container must supply session metadata, physical page sequence,
length framing, page CRC/commit markers and append-only recovery; this format
does not replace those requirements. Do not place TCB3 inside TPD2 as though it
were a frozen V2 record. New outer page/session magic and a reviewed flash writer
are separate work. Retain 14 data pages/sector for that first implementation.

## Host use and conformance

`python tools/compact_decode.py file.tcb3` emits decoded JSON; 64-bit JSON values
follow the existing decoder's decimal-string convention. `--raw-output new.bin`
writes exact reconstructed V2 wire records and refuses to overwrite a file.
This is wire-record data, not a full V2 flash image. Future Web Serial and Lab
decoders must preserve uint64 integers (BigInt in JavaScript), masks and invalid
observations. No physical device is accessed by this tool.

Run tests/compact/run_tests.py and tests/compact/test_decoder.py. The ARM harness
uses production encoder code, existing benchmark sources and V2 golden inputs;
each reconstructed canonical byte is compared. Checked fixtures include the
TCB3 block, expected V2 wire bytes and SHA-256 manifest. The host inverse uses
the benchmark's independently exercised integer decoder, followed by the frozen
V2 semantic validator; it is not a second embedded C++ decoder.
