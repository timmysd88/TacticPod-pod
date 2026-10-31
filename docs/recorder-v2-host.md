# V2 host decoder and conformance package

Phase 1 contains a standard-library Python 3.10+ decoder, independent fixture
authoring script, committed binary/expected JSON fixtures, and automated tests.
It has **no serial dependency, flash writer, firmware linkage or device access**.
The normative byte contract is [recorder-v2-format.md](recorder-v2-format.md).

## Commands

From the project root, `python` can be the existing PlatformIO Python executable:

```powershell
python tools/tacticpod_decode.py tests/recorder_v2/fixtures/mixed_v1_v2.bin
python tools/tacticpod_decode.py tests/recorder_v2/fixtures/mixed_v1_v2.bin --format imu-csv
python tools/tacticpod_decode.py captured-flash.bin --output decoded.json
python tests/recorder_v2/run_tests.py
```

`--output` creates a new host file and refuses an existing output file or input
overwrite. Without it, output goes to stdout. Binary inputs are opened read-only.
JSON includes sessions, their physical sectors, metadata, records, optional footer,
and integrity issues. No export framing or device command is inferred.

Exit 0 means no error-level integrity/structure issues. It does NOT mean every
session was cleanly stopped: interruption/unknown optional records produce
warnings. Exit 2 means corruption, invalid structure, invalid command usage, or
unreadable input/output. Always inspect session status and issues. For CSV, issues
go to stderr so they do not corrupt the table. Existing output files are preserved
and output-file errors return 2; the input remains untouched.

Library use, with exact Python integer values:

```python
from pathlib import Path
from tools.tacticpod_decode import decode_image, imu_csv, json_document

result = decode_image(Path("captured-flash.bin").read_bytes())
for session in result["sessions"]:
    for record in session["records"]:
        if record["type"] == "GPS_RMC" and record["fix_valid"]:
            print(record["timestamp_us"], record["position"])
```

The schema string is `tacticpod.decode/1`. Library integers retain all 64 bits.
`json_document(result)` and CLI JSON represent **every u64 field as a base-10
string**, including counters in `committed_by_type`/`dropped_by_type`, event detail
and microsecond timestamps. Missing fields remain JSON null. u32/i32/u16 values
remain JSON numbers. This avoids silent JavaScript Number rounding. Golden
expected JSON uses exactly the same external contract, authored independently.

An RMC record separates `fields` (reported fixed-point values) from usable
`position`, `speed_m_s` and `course_deg`. Void/N-mode fixes have null usable
values even if the sentence included coordinates. A missing UTC field is null,
not midnight or epoch zero. GGA output is independent; HDOP is dimensionless.
The reference decoder does not infer sample rate, attach stale quality to RMC,
estimate measurement latency, rotate axes, or interpolate missing samples.

## Fixtures

`tests/recorder_v2/fixtures/` contains these committed `.bin` and `.expected.json`
pairs. `sha256.json` identifies both binary and expected output bytes.

| Fixture | Coverage |
|---|---|
| mixed_v1_v2 | V1 IMU and V2 session across two sectors; all four types; signed extrema; valid/void RMC; GGA HDOP; opaque identities; event detail above JS safe integer; u32 micros boundary |
| timestamp_rollover | u64 microseconds spanning the u32 millis boundary; legacy CSV truncation |
| interrupted | One valid page followed by a page body without a commit marker; no footer; prior data survives |
| corrupt_crc | One valid page followed by payload damage with the original CRC; rejected page does not leak records |
| missing_gps | Void RMC with no coordinates/speed/course/date/time; all usable values unavailable |
| empty_session | Valid header/footer with zero records and unavailable first/last timestamps |

The generator constructs bytes and expected logical records independently; it
does NOT call/import the decoder. Recreate only when intentionally reviewing
fixture changes:

```powershell
python tests/recorder_v2/generate_fixtures.py
```

Tests never regenerate fixtures automatically. They check committed files against
fresh in-memory generator output and the SHA manifest, compare complete decoded
JSON, assert literal byte vectors, and exercise CRC corruption at every byte of
a data page's CRC-covered body. Additional tests repair CRCs after structural
mutations to ensure CRC alone cannot admit invalid records. Fixtures are synthetic
host data, not captured/exported hardware recordings.

## app.tacticpod.co.uk and TacticPod Lab

Both consumers should implement this exact binary schema and use the same golden
fixtures. No existing app/Lab implementation is assumed by this package.

For browser binary decoding:

- Use explicit little-endian DataView reads, including `getBigUint64(offset, true)`.
- Keep timestamps/counters as BigInt; subtract a nearby session origin before
  conversion to Number for chart coordinates. Do not convert absolute u64 values
  or event detail to Number first.
- Convert JSON u64 decimal strings using BigInt. Never parse them through Number.
- Preserve original image bytes, physical offsets, identities, unknown records,
  and integrity reports alongside derived data.
- Distinguish clean close, interrupted recording, damaged data, and actual record
  loss. A valid fix does not itself prove continuously advancing UTC or 10Hz.
- Keep raw/invalid observations in downloadable analysis data; filter unusable
  positions explicitly when drawing a track.
- Store raw monotonic and UTC domains independently. Any fitted clock alignment,
  quality join, latency correction or interpolation must be derived, labelled,
  and reversible.

Offline Lab should provide the same schema and preserve integer precision,
including in dataframe/file conversions. Use the legacy IMU CSV only for existing
consumers; it cannot represent all stream types or full timestamp precision.

Future USB/Web Serial transport needs its own versioned framing, physical offset
mapping, response lengths, integrity, backpressure, cancellation and resumability.
The current tool reads address-zero flash-image files; it does not open COM8 or
introduce new firmware RECORD commands. App integration and hardware extraction
remain later phases.
