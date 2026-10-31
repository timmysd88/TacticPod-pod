# TacticPod recorder binary specification 2.0

Status: **frozen Phase 1 host contract**, 2026-10-30. This document is normative
for the supplied fixtures and standalone decoder. No V2 firmware writer, serial
command, sensor acquisition change, migration, or device programming is included.
Changes to this contract require a version decision and new conformance fixtures.

## 1. Scope and compatibility

V2 stores typed IMU_RAW, GPS_RMC, GPS_GGA_QUALITY, and EVENT observations on the
existing 16MiB SPI NOR. It preserves the physical allocation, CRC algorithm,
commit-marker position and append-only recovery rules documented for V1 in
[recording.md](recording.md) and implemented by `src/recorder_format.h`.

The new host reader supports both versions in one image. Existing firmware and
its RECORD READ command are unchanged. A future firmware reader must implement
both versions before a hardware V2 writer is enabled. Old firmware skips V2
headers; it cannot decode V2 data. There is no in-place conversion.

Normative language: MUST is required for 2.0 conformance; SHOULD is a recommended
writer policy. These statements describe future writers, not already implemented
firmware functionality.

## 2. Primitives, physical layout, and commit

All numeric fields are little-endian, with no implicit alignment or struct
padding. `u8/u16/u32/u64` and `i16/i32` are exact-width unsigned and two's-complement
signed integers. Strings/identifiers are byte arrays, not endian-swapped integers.
No floating-point values are stored. Unknown values use masks, never guessed
numbers; zero coordinates and zero speed are valid when their presence bits set.

| Area | Inclusive addresses | Size |
|---|---|---:|
| Recording | `0x000000`–`0xFFEFFF` | 16,773,120 bytes; 4,095 sectors |
| Existing diagnostics | `0xFFF000`–`0xFFFFFF` | 4,096 bytes; not recording data |

Each sector is 4,096 bytes: page 0 header, pages 1–14 data, page 15 optional final
footer. Each page is 256 bytes. A session may span sectors; earlier continuation
sectors need no footer. The final sector gets a footer on a verified close.
Unused pages and unused page payload bytes are `0xFF`.

| Absolute page offset | Size | Field |
|---:|---:|---|
| 0 | 4 | ASCII magic `TPH2`, `TPD2`, or `TPE2` |
| 4 | 4 | Nonzero local session ID, unchanged across its sectors |
| 8 | 4 | Data-page sequence/checkpoint, defined below |
| 12 | 2 | Record count; 0 on headers/footers |
| 14 | 2 | Payload bytes used, 0–232; type-specific bounds apply |
| 16 | 232 | Payload, followed by `0xFF` to byte 247 |
| 248 | 4 | CRC-32/IEEE of exactly bytes 0–247 |
| 252 | 4 | Commit word `0xC0DEC0DE`, bytes `DE C0 DE C0` |

CRC parameters: reflected polynomial `0xEDB88320`, init/final XOR `0xFFFFFFFF`,
refin/refout true. This is Python `zlib.crc32(page[:248])`; check vector
`123456789` → `0xCBF43926`. CRC and marker themselves are excluded from the CRC.

Writers MUST verify the entire destination sector is blank before claiming it,
verify each destination page is blank, program/read back the body with marker
still `0xFFFFFFFF`, then program the marker separately and verify the committed
page. A body CRC without its marker is not committed. No program crosses a page
boundary. No recording operation addresses the diagnostic sector.

A page is a transaction unit: if its CRC, envelope, or a known record is invalid,
none of that page's records are exported. CRC detects corruption, not authenticity.

## 3. Sequences and identities

Local session IDs are nonzero u32 display identifiers, not globally unique. A
future writer may retain V1's first-sector-index-plus-one allocation policy.
They can repeat after erase. Session UUID + boot UUID + device ID identify a V2
session outside its original flash image. UUIDs are opaque 16-byte tokens; this
format does not require RFC UUID field-endian transformations or version bits.
They MUST not be reused for a new session/boot. The generation policy is deferred
to firmware; zero-filled identity/hash arrays are forbidden in 2.0.

The first header has sector index 0 and page-sequence checkpoint 0. Data-page
sequence starts at 0, increments by one per committed data page, and never resets
at a sector boundary. Header checkpoint is the first data sequence expected in
that sector. A footer checkpoint is the next sequence after the last data page.
Header/footer pages do not consume data sequences. Wrapping is forbidden within
a session (physical capacity makes it unnecessary).

Continuation sectors use increasing sector indices and identical session metadata
except their sector index/checkpoint. They follow in physical sector order. A
writer SHOULD fill pages 1–14 before advancing; abandoning blank tail pages is
allowed, but never filling them later. A footer terminates the session. Reset
never resumes an interrupted session: the next recording gets a new identity.

## 4. Mandatory header metadata: exact fit

The TPH2 fixed payload is **196 bytes** at page offsets 16–211. An additional
**36 bytes** are available at 212–247 for optional TLVs. Header record count is
zero; payload-used is `196 + tlv_bytes`, at most 232. Thus the full mandatory
metadata, envelope, CRC and marker fit one existing 256-byte page without a
metadata side partition or extra page.

| Page offset | Size/type | Field / required interpretation |
|---:|---|---|
| 16 | u16 | Format major = 2 |
| 18 | u16 | Format minor = 0 |
| 20 | u32 | Zero-based sector index within session |
| 24 | u64 | Session start, device monotonic microseconds |
| 32 | 8 bytes | Device ID, opaque stable bytes, not all zero |
| 40 | 16 bytes | Session UUID, not all zero |
| 56 | 32 bytes | SHA-256 of the exact firmware application binary, not all zero |
| 88 | 16 bytes | Boot UUID, not all zero |
| 104 | u32 | Intended software IMU rate, millihertz; initially 100000 |
| 108 | u32 | Configured sensor ODR, millihertz; initially 104000 |
| 112 | u32 | Acceleration sensitivity, micro-g/LSB; initially 61 |
| 116 | u32 | Angular-rate sensitivity, micro-degrees/s/LSB; initially 8750 |
| 120 | u32 | Accelerometer full-scale magnitude, milli-g; initially 2000 |
| 124 | u32 | Gyroscope configured range label, milli-degrees/s; initially 245000 |
| 128 | u32 | GPS requested navigation period, microseconds; initially 100000 |
| 132 | u32 | GGA output divisor in navigation epochs; initially 10 |
| 136 | u32 | GPS UART baud; initially 38400 |
| 140 | u32 | Required features, exactly `0x0000001F` in 2.0 |
| 144 | u64 | Last GPS profile check timestamp; `UINT64_MAX` if unknown |
| 152 | u8 | GPS profile state: 0 unknown, 1 verified matching, 2 check failed |
| 153 | u8 | Timestamp basis = 1, software observation/read-start clock |
| 154 | u8 | Calibration convention = 0, uncorrected raw sensor values |
| 155 | u8 | Axis convention = 1, sensor-native X/Y/Z register order |
| 156 | 16 bytes | IMU model ASCII, zero-padded; initially `LSM6DS3TR-C` |
| 172 | 16 bytes | GPS model ASCII, zero-padded; initially `MAX-M10S` |
| 188 | 16 bytes | GPS firmware ASCII, zero-padded; e.g. `SPG 5.10` |
| 204 | u32 | Reserved metadata flags = 0 |
| 208 | u16 | `tlv_bytes`, 0–36 inclusive |
| 210 | u16 | Reserved = 0 |
| 212 | up to 36 bytes | Metadata TLVs, then `0xFF` |

Rates, scales, ranges, period, divisor and baud MUST be nonzero. The range field
records the configured library range label and is not a substitute for the
separate conversion sensitivity. Axis convention 1 is not a claimed earth/body
frame rotation. Unknown model/firmware strings may be empty zero-filled strings.
If a string uses a null terminator, all following bytes in that field MUST be zero.

Required feature bits: bit 0 IMU_RAW, bit 1 GPS_RMC, bit 2 GPS_GGA_QUALITY, bit 3
EVENT, bit 4 shared u64 microsecond clock. They identify required decoder
capabilities, not a promise that every stream was available in every session.
Other bits are unsupported. The supplied reader accepts only major/minor 2.0.

For GPS state 0 the check timestamp MUST be UINT64_MAX. For states 1/2 it MUST
be no later than session start. State 1 is historical verification, not a promise
that a GPS-only reset cannot occur later. Subsequent changes belong in EVENTs.

A TLV is `u16 tag, u16 byte_length, byte[byte_length] value`; no alignment/padding
inside the TLV area. Tag 0 is invalid. Tag 1 is an optional UTF-8 firmware build
label, at most once, at most 32 bytes including no terminator. Unknown optional
tags are preserved/skipped by length. Tag bit 15 means mandatory; there are no
defined mandatory TLVs in 2.0, so all such tags are rejected. Tag 1 is not a
replacement for the binary hash. Duplicate unknown optional tags are retained in
their original order. Extensions MUST repeat identically in continuation headers.

## 5. Shared record header and clocks

| Record-relative offset | Size/type | Field |
|---:|---|---|
| 0 | u8 | Type: 1 IMU_RAW, 2 GPS_RMC, 3 GPS_GGA_QUALITY, 4 EVENT |
| 1 | u8 | Record schema version, initially 1 |
| 2 | u16 | Total record bytes including these 12 bytes |
| 4 | u64 | Device monotonic microseconds since boot |
| 12 | variable | Type-specific fields |

Data payload is exactly the concatenation of `record_count` complete records.
Records MUST NOT cross a page boundary. No per-record padding is inserted. The
sum of lengths MUST equal payload-used. Minimum structural record size is 12.
Unknown optional types/schema versions are preserved as opaque data and reported
as warnings, never decoded under the wrong schema. They still occupy their page
sequence and participate in chronological order and first/last timestamps.

Timestamps MUST be nondecreasing within a session and no earlier than session
start; equal timestamps are permitted. UINT64_MAX is reserved and cannot be a
record/session/stop timestamp. The clock MUST be extended across the underlying
32-bit micros/millis rollover and MUST run even while recording is stopped.

Timestamp basis 1: IMU timestamp is the start of its sensor read; GPS timestamp
is complete-sentence consumption/checksum validation; EVENT timestamp is detection.
This is observation-time synchronisation. GPS UART transmission/buffering and
receiver latency are not removed. Six IMU register reads are not simultaneous.
No PPS or fixed GPS latency compensation is implied. UTC and monotonic clocks
remain separate. No timestamps from separate boot UUIDs may be concatenated as
one monotonic timeline without an explicit host alignment model.

## 6. IMU_RAW (type 1, schema 1, 24 bytes)

Offsets 12,14,16,18,20,22: six i16 values, accelerometer X/Y/Z then gyroscope
X/Y/Z. No bias, orientation or calibration correction is applied. Convert using
header sensitivities: `g = raw * accel_ug_per_lsb / 1e6`,
`degrees_per_second = raw * gyro_udps_per_lsb / 1e6`.

Nominal 100Hz software acquisition and the configured 104Hz ODR are both recorded;
the format does not imply perfect sensor timing or invent replacement samples.

## 7. GPS_RMC (type 2, schema 1, 44 bytes)

| Record offset | Type | Field |
|---:|---|---|
| 12 | u32 | Validity/presence mask |
| 16 | i32 | Latitude, degrees × 10^7 |
| 20 | i32 | Longitude, degrees × 10^7 |
| 24 | u32 | Reported ground speed, mm/s |
| 28 | u32 | Reported course over ground, degrees × 10^5 |
| 32 | u32 | UTC date, decimal YYYYMMDD |
| 36 | u32 | UTC time of day, milliseconds |
| 40 | u8 | Raw RMC status: ASCII A/V, or 0 if unrecognised/absent |
| 41 | u8 | Optional mode, uppercase ASCII when present, otherwise 0 |
| 42 | 2 bytes | Uppercase ASCII talker ID, e.g. GP/GN |

| Mask bit | Meaning |
|---:|---|
| 0 | Latitude/longitude fields present, parsed and in range |
| 1 | Usable fix for this observation |
| 2 | Speed field present and parsed |
| 3 | Course field present and parsed |
| 4 | UTC date field present and calendar-valid |
| 5 | UTC time field present and valid |
| 6 | Mode field present |
| 7–31 | Reserved; MUST be zero |

Usable-fix bit MUST equal `(coordinates_present AND status == 'A' AND mode != 'N')`;
an absent mode does not invalidate status A. Presence bits do not assert fix
validity. Preserve source fields on a checksum-valid void sentence if available,
but expose position/speed/course as **unusable** when usable-fix is clear. The
host decoder retains the reported fixed-point fields separately from normalised
usable fields. Other reported modes remain visible; consumers may apply stricter
policies for estimated/simulated data. Course is ground track, not body heading.

Coordinate limits are ±90×10^7 latitude and ±180×10^7 longitude. Course is
0–35,999,999 inclusive. Speed has the full u32 representable range. When bit 0 is
clear BOTH coordinates MUST be zero; when any other presence bit is clear its
numeric field MUST be zero. Status A without coordinates is not a usable fix.

UTC dates are calendar-valid years 2000–2099. A future NMEA encoder maps two-digit
RMC years into this explicit century. Time-of-day range is 0–86,399,999 inclusive.
Leap-second notation is not representable in 2.0: clear the time-present bit,
store zero, and emit a UTC-discontinuity EVENT. Do not fold `23:59:60` into the
next date or normalise it silently. Date/time validity is field-scoped, never
inherited from TinyGPSPlus caches. UTC may be absent while fix fields remain
useful in the device-clock domain.

Every checksum-valid RMC observation during recording SHOULD be retained,
including void, duplicate-UTC and backwards-UTC observations. Do not infer
advancing navigation epochs from message count. Checksum-invalid sentences are
counted in the footer/events but do not become navigation records.

No accuracy, covariance, error radius or inferred navigation validity is stored.

## 8. GPS_GGA_QUALITY (type 3, schema 1, 40 bytes)

| Record offset | Type | Field |
|---:|---|---|
| 12 | u32 | Presence mask |
| 16 | u32 | Associated UTC date YYYYMMDD, optional |
| 20 | u32 | GGA UTC time of day, milliseconds |
| 24 | i32 | Reported altitude above mean sea level, millimetres |
| 28 | i32 | Reported geoid separation, millimetres |
| 32 | u16 | HDOP × 100, dimensionless |
| 34 | u16 | Satellites used |
| 36 | u8 | Reported quality code, 0–254; 255 means missing/unparsed |
| 37 | 2 bytes | Uppercase ASCII talker ID |
| 39 | u8 | Reserved = 0 |

Presence bits: 0 UTC time; 1 associated date; 2 altitude; 3 geoid separation;
4 HDOP; 5 satellites. Bits 6–31 MUST be zero. An absent field MUST be zero;
presence, not zero/nonzero, determines availability. Date-present requires
time-present. UTC limits are identical to RMC. Signed millimetre fields support
negative altitude/separation. HDOP and satellites have full u16 ranges; a future
encoder must reject out-of-range input, never silently clip/wrap it.

GGA has no source date. Set the date bit only using an internally consistent,
recent RMC date/time association, including midnight handling; otherwise leave
it absent. Maximum association age for 2.0 writers is 1.5 seconds of device time.
This provenance bit means derived date, not a date actually transmitted by GGA.

Quality is independent of RMC validity; no GGA quality code upgrades a void RMC.
Preserve unknown codes, do not assign undocumented meanings. Quality 0 means
no fix; reported fields may still be present but are not proof of a valid fix.
HDOP MUST NOT be converted into accuracy in metres. Altitude is MSL, not
ellipsoidal height, and no vertical accuracy is invented.

Keep 1Hz GGA records separate from 10Hz RMC. Host association prefers equal UTC;
otherwise a preceding quality observation may be displayed up to age 1.5 seconds,
with association method and age exposed. Do not persistently copy quality into
later navigation records. The reference decoder deliberately does not perform
joins, interpolation, or sample-rate inference.

## 9. EVENT (type 4, schema 1, 32 bytes)

| Record offset | Type | Field |
|---:|---|---|
| 12 | u16 | Event code, nonzero |
| 14 | u8 | Severity: 0 information, 1 warning, 2 error, 3 fatal |
| 15 | u8 | Stream: 0 system, 1 IMU, 2 RMC, 3 GGA, 4 EVENT |
| 16 | u32 | Code-specific argument |
| 20 | u32 | Number of occurrences, split into multiple events if overflow |
| 24 | u64 | Code-specific detail; all values allowed |

| Code | Meaning | argument | count | detail |
|---:|---|---|---|---|
| 1 | Dropped records | cause: 0 unknown, 1 overflow, 2 full, 3 fault | records | first affected device us, or UINT64_MAX |
| 2 | Missed IMU deadlines | nominal interval us | missed intervals | first affected device us or UINT64_MAX |
| 3 | GPS silence | configured silence threshold ms | silence episodes | last valid GPS observation us or UINT64_MAX |
| 4 | GPS profile state | 0 unknown, 1 verified, 2 failed | transitions | check/detection device us |
| 5 | UTC discontinuity | 0 unknown, 1 backwards, 2 leap notation, 3 reset, other values preserved | observations | previous UTC milliseconds-of-day, or UINT64_MAX |
| 6 | Queue overflow | queue capacity in records | rejected records | first affected device us or UINT64_MAX |
| 7 | Storage full | usable bytes remaining | rejected records | append flash address |
| 8 | Monotonic clock anomaly | 0 unknown, 1 extension fault | detections | previous device us or UINT64_MAX |

Unknown nonzero codes are retained with their raw arguments. Events record facts,
not reconstructed missing measurements. An overflow event can accompany a dropped
records event describing the same loss; consumers MUST NOT sum event counts to
derive loss totals. Footer counters are authoritative when verified.

## 10. Clean-stop footer (TPE2)

Footer count is zero; payload-used is exactly **120 bytes**. Bytes 136–247 are
`0xFF`. Its checkpoint is the next expected data-page sequence for the session.

| Page offset | Type | Field |
|---:|---|---|
| 16 | u64 | Stop device timestamp |
| 24 | u32 | Close reason: 0 user STOP, 1 storage full, 2 queue overflow, 3 controlled fault |
| 28 | u32 | Flags: bit 0 loss/missed IMU present, bit 1 GPS anomaly detected; others 0 |
| 32,40,48,56 | 4 × u64 | Committed record counts: IMU, RMC, GGA, EVENT |
| 64,72,80,88 | 4 × u64 | Dropped record counts in the same order |
| 96 | u64 | Missed IMU acquisition intervals (distinct from dropped queued records) |
| 104 | u64 | GPS checksum failures observed while recording |
| 112 | u64 | Stored RMC records with usable-fix bit clear |
| 120 | u64 | First stored record timestamp, UINT64_MAX if no records |
| 128 | u64 | Last stored record timestamp, UINT64_MAX if no records |

Counts cover the whole session and become committed only after page marker
verification. A complete footer MUST agree with decoded supported record counts,
invalid-fix count, sequence and first/last timestamps. Unknown schemas of known
types still count toward that type. Future unknown type IDs are outside the four
counters, but remain included in first/last timestamps. The reader cannot check
invalid-fix semantics for an unknown RMC schema, so it skips only that semantic
cross-check and reports unknown records.

Stop >= last record >= session start. For an empty session, both first/last
timestamps are UINT64_MAX. Flag bit 0 MUST match nonzero dropped counters or
missed intervals. GPS checksum failures SHOULD also set bit 1; source no-fix by
itself is not necessarily an anomaly. A verified close can still report losses;
`closed` is not a claim of zero loss. Reset during fault handling may prevent a
footer; do not fabricate one.

## 11. Recovery and decoder policy

The reader accepts a file beginning at physical recording address zero: a
sector-aligned recording-area prefix, the full recording area, or a 16MiB dump.
It ignores the diagnostic sector. It reports a truncated final sector rather than
treating it as a complete sector. File offsets are physical flash byte addresses.
This reader does not implement device extraction or a session transfer container.

1. Blank sectors are skipped. Nonblank sectors without a supported committed
   header are reported, never interpreted as free space or unowned sample data.
2. V1 and V2 headers select separate parsers. Metadata/version mismatch is not
   silently accepted as a compatible layout.
3. Read data pages 1–14 in order. Stop that sector at its first blank, uncommitted,
   corrupt or structurally invalid data page. Report nonblank content following
   a blank page; do not export it. Retain already validated earlier pages.
4. Examine subsequent sector headers independently. Continuation metadata and
   checkpoints establish ownership/order; sequence gaps are reported. A surviving
   continuation without its original prefix is salvageable but incomplete.
5. A valid final footer closes a fully recovered session only when counts and
   timestamps agree. Torn/missing footer does not invalidate prior data pages.
6. Sessions are `closed`, `interrupted`, or `damaged`. CRC/structure/sequence errors
   give `damaged`; an uncommitted tail or missing prefix/footer gives `interrupted`
   unless another error already establishes damage. Footer may be retained as
   evidence for an incomplete session without claiming a clean recovery.

No reader changes flash. Writers MUST conservatively retain occupied sectors and
do a full blank check before any future append. Existing recordings are not
overwritten to repair a damaged session. CRC does not reveal how many records
remained in RAM at power loss; exact tail-loss counts may be unknowable.

## 12. V1 decoding and legacy IMU CSV

V1 is defined by the existing firmware: TPH1/TPD1/TPE1, common CRC/marker offsets,
u32 session and page sequence, u32 count at 12, 16-byte IMU samples at 16.
Header fields at 16/20/24/28/32 are version=1, rate=100, accel=61, gyro=8750,
session-start milliseconds. Reader rejects other V1 versions/scales. Data count
is 1–14. Footer count at 12 is total committed samples, losses at 16, misses at 20.

V1 timestamps retain their original u32 `millis()` values. Do not invent boot UUIDs,
GPS records, or an unambiguous u64 clock from historical V1 sessions. A raw wrap
from `0xFFFFFFF0` to `0x10` is preserved. The supplied fixtures exercise this.

Host `--format imu-csv` preserves the existing RECORD READ column names and units:

```csv
session,timestamp_ms,accel_x_g,accel_y_g,accel_z_g,gyro_x_dps,gyro_y_dps,gyro_z_dps
```

Only decoded IMU records appear. For V2 the compatibility timestamp is
`(timestamp_us // 1000) & 0xFFFFFFFF`. Use JSON/binary for full precision and clock
identity. The host applies recorded scales with six decimal places; it does not
promise bit-identical rounding to the firmware's float-based CSV implementation.
Host CSV omits console BEGIN/END markers. The actual firmware RECORD READ output
and its supported versions remain unchanged in Phase 1.

## 13. Host/app contract and capacity

[recorder-v2-host.md](recorder-v2-host.md) documents Python use, output, fixtures
and the integration contract for app.tacticpod.co.uk and TacticPod Lab. Raw binary
and this spec are authoritative. Future binary USB framing is separate from
flash-page format; it MUST preserve page bytes and original addresses, support
bounded reads/resume, and exclude console interleaving. No such command exists yet.

At nominal rates: IMU 100×24=2400 B/s, RMC 10×44=440 B/s, GGA 1×40=40 B/s;
total record bytes=2880 B/s before events. There are 57,330 data pages.
Ideal packing gives 76.97 minutes; a simple chronological next-fit model gives
13.5 data pages/s and **70.78 minutes** from empty flash. Short sessions, events,
partial-page timeouts, and existing data reduce this. No measured throughput or
timing guarantee is implied. The later 120-minute target requires a newly
versioned packing/compression scheme; do not silently alter these record sizes.

## 14. Deferred hardware decisions

Phase 1 freezes bytes/semantics, not acquisition or flash scheduling code. Before
hardware writing: implement clock extension/boot/session identity generation,
sentence-scoped parser provenance, measured SPI/USB/IMU timing, queue/flush/loss
policies, and dual-version firmware export. Confirm the device-axis mapping and
binary-hash provisioning. PPS alignment, explicit UBX accuracy, calibration
application, leap-second encoding, and compression require separate review and
feature/schema decisions. No supplied field claims these capabilities today.
