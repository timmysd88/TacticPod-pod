"""Read-only TacticPod V1/V2 flash-image decoder. Python 3.10+, standard library.

Wire contract: docs/recorder-v2-format.md. No serial access or device writes.
"""
from __future__ import annotations

import argparse
import csv
import datetime as dt
import io
import json
from pathlib import Path
import struct
import sys
import zlib

PAGE = 256
SECTOR = 4096
RECORDING_END = 0xFFF000
COMMIT = 0xC0DEC0DE
TYPES = {1: "IMU_RAW", 2: "GPS_RMC", 3: "GPS_GGA_QUALITY", 4: "EVENT"}
LENGTHS = {1: 24, 2: 44, 3: 40, 4: 32}
U64_MAX = (1 << 64) - 1


class FormatError(ValueError):
    """A page is unusable as a unit; no records from it may be emitted."""


def require(condition, message):
    if not condition:
        raise FormatError(message)


def unpack(fmt, data, offset=0):
    return struct.unpack_from("<" + fmt, data, offset)


def integrity(page):
    require(len(page) == PAGE, "truncated page")
    require(unpack("I", page, 252)[0] == COMMIT, "missing commit marker")
    require(unpack("I", page, 248)[0] == zlib.crc32(page[:248]), "CRC mismatch")


def ascii_field(value):
    before, separator, after = value.partition(b"\0")
    require(not separator or not any(after), "nonzero string padding")
    require(all(32 <= c <= 126 for c in before), "invalid ASCII metadata")
    return before.decode("ascii")


def envelope(page, magic):
    integrity(page)
    require(page[:4] == magic, "wrong page magic")
    session, sequence, count, used = unpack("IIHH", page, 4)
    require(session > 0 and used <= 232, "invalid page envelope")
    require(page[16 + used:248] == b"\xff" * (232 - used), "noncanonical page padding")
    return session, sequence, count, page[16:16 + used]


def header_v2(page):
    sid, sequence, count, payload = envelope(page, b"TPH2")
    require(count == 0 and len(payload) >= 196, "invalid header length/count")
    major, minor, sector_index = unpack("HHI", page, 16)
    require((major, minor) == (2, 0), "unsupported format version")
    names = ("imu_rate_millihz", "imu_odr_millihz", "accel_ug_per_lsb",
             "gyro_udps_per_lsb", "accel_range_mg", "gyro_range_mdps",
             "gps_period_us", "gga_epoch_divisor", "uart_baud", "required_features")
    metadata = dict(zip(names, unpack("10I", page, 104)))
    require(metadata["required_features"] == 0x1F, "unsupported required feature set")
    require(all(metadata[k] > 0 for k in names[:-1]), "zero metadata scale/rate")
    start, = unpack("Q", page, 24)
    require(start < U64_MAX, "reserved session timestamp")
    verified, = unpack("Q", page, 144)
    state, basis, calibration, axes = unpack("4B", page, 152)
    flags, tlv_size, reserved = unpack("IHH", page, 204)
    require(state <= 2 and basis == 1 and calibration == 0 and axes == 1,
            "unsupported timestamp/calibration/axis metadata")
    require(flags == 0 and reserved == 0, "nonzero reserved header field")
    require((state == 0 and verified == U64_MAX) or
            (state in (1, 2) and verified <= start), "invalid profile verification timestamp")
    require(tlv_size <= 36 and len(payload) == 196 + tlv_size, "invalid metadata TLV length")
    for a, b in ((32, 40), (40, 56), (56, 88), (88, 104)):
        require(any(page[a:b]), "missing mandatory identity/hash")
    metadata.update({
        "format_major": major, "format_minor": minor,
        "session_start_us": start, "device_id_hex": page[32:40].hex(),
        "session_uuid_hex": page[40:56].hex(), "firmware_sha256": page[56:88].hex(),
        "boot_uuid_hex": page[88:104].hex(), "gps_profile_state": state,
        "gps_profile_checked_us": None if verified == U64_MAX else verified,
        "timestamp_basis": basis, "calibration": calibration, "axis_convention": axes,
        "imu_model": ascii_field(page[156:172]), "gps_model": ascii_field(page[172:188]),
        "gps_firmware": ascii_field(page[188:204]), "extensions": [],
    })
    offset = 212
    while offset < 212 + tlv_size:
        require(offset + 4 <= 212 + tlv_size, "truncated metadata TLV")
        tag, size = unpack("HH", page, offset)
        offset += 4
        require(tag != 0 and offset + size <= 212 + tlv_size, "invalid metadata TLV")
        require(not tag & 0x8000, "unsupported mandatory metadata TLV")
        data = page[offset:offset + size]
        if tag == 1:
            require(not any(e["tag"] == 1 for e in metadata["extensions"]), "duplicate build label")
            try:
                label = data.decode("utf-8")
            except UnicodeDecodeError as exc:
                raise FormatError("invalid UTF-8 build label") from exc
            metadata["extensions"].append({"tag": tag, "value": label})
        else:
            metadata["extensions"].append({"tag": tag, "raw_hex": data.hex()})
        offset += size
    return sid, sequence, sector_index, metadata


def utc_fields(mask, date_bit, time_bit, date, tod):
    require(bool(mask & date_bit) or date == 0, "unavailable UTC date must be zero")
    require(bool(mask & time_bit) or tod == 0, "unavailable UTC time must be zero")
    date_text = None
    if mask & date_bit:
        year, month, day = date // 10000, date // 100 % 100, date % 100
        require(2000 <= year <= 2099, "UTC year outside V2.0 range")
        try:
            date_text = dt.date(year, month, day).isoformat()
        except ValueError as exc:
            raise FormatError("invalid UTC date") from exc
    if mask & time_bit:
        require(tod < 86400000, "invalid UTC time; leap-second representation unsupported")
    return {"date": date_text, "time_of_day_ms": tod if mask & time_bit else None}


def decode_record(data):
    kind, version, size, timestamp = unpack("BBHQ", data)
    require(size == len(data) and size >= 12, "invalid record length")
    require(timestamp < U64_MAX, "reserved timestamp value")
    base = {"type": TYPES.get(kind, "UNKNOWN"), "type_id": kind,
            "schema_version": version, "timestamp_us": timestamp}
    if kind not in TYPES or version != 1:
        base.update(type="UNKNOWN", raw_hex=data[12:].hex())
        return base
    require(size == LENGTHS[kind], "wrong known-record size")
    if kind == 1:
        base["axes_raw"] = list(unpack("6h", data, 12))
    elif kind == 2:
        mask, lat, lon, speed, course, date, tod, status, mode, talker = unpack("IiiIIIIBB2s", data, 12)
        require(mask & ~0x7F == 0 and status in (0, 65, 86), "invalid RMC flags/status")
        require(all(65 <= c <= 90 for c in talker), "invalid RMC talker")
        require(not mask & 1 or (-900000000 <= lat <= 900000000 and
                                 -1800000000 <= lon <= 1800000000), "invalid coordinates")
        require(mask & 1 or (lat == 0 and lon == 0), "unavailable coordinates must be zero")
        require(not mask & 2 or (mask & 1 and status == 65), "fix requires coordinates and RMC A")
        require(bool(mask & 2) == bool(mask & 1 and status == 65 and not (mask & 64 and mode == 78)),
                "inconsistent RMC fix flag")
        require(mask & 4 or speed == 0, "unavailable speed must be zero")
        require((mask & 8 and course < 36000000) or (not mask & 8 and course == 0), "invalid course")
        require((mask & 64 and 65 <= mode <= 90) or (not mask & 64 and mode == 0), "invalid mode")
        base.update(validity_mask=mask, receiver_status=chr(status) if status else None,
                    mode=chr(mode) if mask & 64 else None, talker=talker.decode("ascii"),
                    utc=utc_fields(mask, 16, 32, date, tod), fix_valid=bool(mask & 2),
                    fields={"latitude_e7": lat if mask & 1 else None,
                            "longitude_e7": lon if mask & 1 else None,
                            "speed_mm_s": speed if mask & 4 else None,
                            "course_e5_deg": course if mask & 8 else None},
                    position={"latitude_deg": lat / 1e7, "longitude_deg": lon / 1e7} if mask & 2 else None,
                    speed_m_s=speed / 1000 if mask & 6 == 6 else None,
                    course_deg=course / 1e5 if mask & 10 == 10 else None)
    elif kind == 3:
        mask, date, tod, altitude, geoid, hdop, sats, quality, talker, reserved = unpack("IIIiiHHB2sB", data, 12)
        require(mask & ~0x3F == 0 and reserved == 0, "invalid GGA flags/reserved")
        require(all(65 <= c <= 90 for c in talker), "invalid GGA talker")
        require(not mask & 2 or mask & 1, "associated GGA date requires UTC time")
        for bit, value in ((4, altitude), (8, geoid), (16, hdop), (32, sats)):
            require(mask & bit or value == 0, "unavailable GGA field must be zero")
        base.update(validity_mask=mask, utc=utc_fields(mask, 2, 1, date, tod),
                    date_is_associated=bool(mask & 2), fix_quality=quality,
                    talker=talker.decode("ascii"),
                    altitude_msl_mm=altitude if mask & 4 else None,
                    geoid_separation_mm=geoid if mask & 8 else None,
                    hdop=hdop / 100 if mask & 16 else None,
                    satellites_used=sats if mask & 32 else None)
    else:
        code, severity, stream, argument, count, detail = unpack("HBBIIQ", data, 12)
        require(code > 0 and severity <= 3 and stream <= 4, "invalid event")
        base.update(code=code, severity=severity, stream=stream, argument=argument,
                    count=count, detail=detail)
    return base


def data_v2(page):
    sid, sequence, count, payload = envelope(page, b"TPD2")
    require(count > 0, "empty data page")
    records, offset = [], 0
    while offset < len(payload):
        require(offset + 12 <= len(payload), "truncated record header")
        size = unpack("H", payload, offset + 2)[0]
        require(size >= 12 and offset + size <= len(payload), "record exceeds page payload")
        records.append(decode_record(payload[offset:offset + size]))
        offset += size
    require(len(records) == count, "record count mismatch")
    return sid, sequence, records


def footer_v2(page):
    sid, sequence, count, payload = envelope(page, b"TPE2")
    require(count == 0 and len(payload) == 120, "invalid footer length/count")
    stop, reason, flags = unpack("QII", payload)
    require(stop < U64_MAX, "reserved stop timestamp")
    require(reason <= 3 and flags & ~3 == 0, "invalid close reason/flags")
    counts = list(unpack("4Q", payload, 16))
    dropped = list(unpack("4Q", payload, 48))
    missed, checksums, invalid, first, last = unpack("5Q", payload, 80)
    require(bool(flags & 1) == bool(any(dropped) or missed), "inconsistent footer loss flag")
    require((first == U64_MAX and last == U64_MAX) or first <= last <= stop,
            "invalid footer record timestamps")
    return sid, sequence, dict(stop_us=stop, close_reason=reason, flags=flags,
                               committed_by_type=counts, dropped_by_type=dropped,
                               missed_imu_intervals=missed, gps_checksum_failures=checksums,
                               invalid_rmc_observations=invalid,
                               first_record_us=None if first == U64_MAX else first,
                               last_record_us=None if last == U64_MAX else last)


def decode_image(image):
    """Decode a recording-area prefix/full flash dump; never return torn-page records.

    Diagnostics are explicit. Committed pages in later sectors remain salvageable.
    Input starts at flash address zero. The diagnostic sector is never decoded.
    """
    result = {"schema": "tacticpod.decode/1", "sessions": [], "issues": []}

    def issue(offset, code, message, severity="error", session=None):
        result["issues"].append(dict(offset=offset, code=code, severity=severity, message=message))
        if session is not None and severity == "error":
            session["status"] = "damaged"

    require(len(image) <= 0x1000000, "image exceeds physical flash size")
    image = image[:RECORDING_END]
    if len(image) % SECTOR:
        issue(len(image) // SECTOR * SECTOR, "truncated_sector", "image ends within a sector")
    current = None
    for offset in range(0, len(image) // SECTOR * SECTOR, SECTOR):
        sector = image[offset:offset + SECTOR]
        if sector == b"\xff" * SECTOR:
            current = None
            continue
        page = sector[:PAGE]
        if page[:4] not in (b"TPH1", b"TPH2"):
            issue(offset, "unknown_header", "nonblank sector has no supported header")
            current = None
            continue
        version = page[3] - ord("0")
        try:
            if version == 2:
                sid, sequence, sector_index, metadata = header_v2(page)
            else:
                integrity(page)
                sid, sequence, sector_index = unpack("III", page, 4)
                fmt, rate, accel, gyro, start = unpack("5I", page, 16)
                require(sid > 0 and (fmt, rate) == (1, 100) and
                        (accel, gyro) in ((61, 8750), (244, 35000)), "unsupported V1 metadata")
                metadata = dict(format_major=1, format_minor=0, imu_rate_millihz=100000,
                                accel_ug_per_lsb=accel, gyro_udps_per_lsb=gyro, session_start_ms=start)
        except FormatError as exc:
            issue(offset, "header_invalid", str(exc))
            current = None
            continue
        continuation = (current is not None and current["version"] == version and
                        current["session_id"] == sid and sector_index == current["_sector_index"] + 1 and
                        current["footer"] is None and metadata == current["metadata"])
        conflicting_identity = (not continuation and current is not None and
                                version == current["version"] == 2 and
                                metadata["session_uuid_hex"] == current["metadata"]["session_uuid_hex"])
        if not continuation:
            current = dict(version=version, session_id=sid, metadata=metadata, sectors=[],
                           records=[], footer=None, status="interrupted", _next=sequence,
                           _sector_index=sector_index)
            result["sessions"].append(current)
            if sector_index != 0 or sequence != 0:
                issue(offset, "missing_prefix", "session prefix unavailable", "warning", current)
                current["_partial"] = True
            if conflicting_identity:
                issue(offset, "continuation_invalid", "reused session identity or conflicting continuation metadata/order",
                      session=current)
        if sequence != current["_next"]:
            issue(offset, "sequence_gap", "continuation header sequence discontinuity", session=current)
        current["_sector_index"] = sector_index
        current["sectors"].append(offset)
        expected = sequence
        for index in range(1, 15):
            at = offset + index * PAGE
            data = sector[index * PAGE:(index + 1) * PAGE]
            if data == b"\xff" * PAGE:
                if sector[(index + 1) * PAGE:15 * PAGE] != b"\xff" * ((14 - index) * PAGE):
                    issue(at, "nonblank_tail", "data after first blank page is not exported", session=current)
                break
            try:
                if version == 2:
                    data_sid, data_seq, records = data_v2(data)
                else:
                    integrity(data)
                    require(data[:4] == b"TPD1", "wrong V1 data magic")
                    data_sid, data_seq, count = unpack("III", data, 4)
                    require(1 <= count <= 14, "invalid V1 sample count")
                    records = []
                    for n in range(count):
                        ms, *axes = unpack("I6h", data, 16 + n * 16)
                        records.append(dict(type="IMU_RAW", type_id=1, schema_version=0,
                                            timestamp_ms=ms, axes_raw=axes))
                require(data_sid == sid and data_seq == expected, "data session/sequence mismatch")
                if version == 2:
                    previous = current["records"][-1]["timestamp_us"] if current["records"] else metadata["session_start_us"]
                    for record in records:
                        require(record["timestamp_us"] >= previous, "backwards monotonic timestamp")
                        previous = record["timestamp_us"]
                current["records"].extend(records)
                for record in records:
                    if record["type"] == "UNKNOWN":
                        issue(at, "unknown_record", "optional record retained without semantic decoding", "warning")
                expected += 1
            except FormatError as exc:
                severity = "warning" if str(exc) == "missing commit marker" else "error"
                issue(at, "data_invalid", str(exc), severity, current)
                current["_partial"] = True
                break
        current["_next"] = expected
        footer = sector[15 * PAGE:]
        if footer != b"\xff" * PAGE:
            try:
                if version == 2:
                    footer_sid, footer_seq, decoded = footer_v2(footer)
                    require(footer_sid == sid and footer_seq == expected, "footer session/sequence mismatch")
                    counts = [sum(r["type_id"] == t for r in current["records"]) for t in range(1, 5)]
                    require(decoded["stop_us"] >= metadata["session_start_us"], "stop precedes session")
                    if not current.get("_partial"):
                        require(counts == decoded["committed_by_type"], "footer record counts mismatch")
                        if all(r["schema_version"] == 1 for r in current["records"] if r["type_id"] == 2):
                            invalid = sum(r["type"] == "GPS_RMC" and not r["fix_valid"] for r in current["records"])
                            require(invalid == decoded["invalid_rmc_observations"], "footer invalid-fix count mismatch")
                        if current["records"]:
                            require((decoded["first_record_us"], decoded["last_record_us"]) ==
                                    (current["records"][0]["timestamp_us"], current["records"][-1]["timestamp_us"]),
                                    "footer record timestamp mismatch")
                        else:
                            require(decoded["first_record_us"] is None and decoded["last_record_us"] is None,
                                    "empty session footer timestamps must be unavailable")
                else:
                    integrity(footer)
                    require(footer[:4] == b"TPE1", "wrong V1 footer magic")
                    footer_sid, footer_seq, total, lost, missed = unpack("5I", footer, 4)
                    require(footer_sid == sid and footer_seq == expected, "V1 footer session/sequence mismatch")
                    if not current.get("_partial"):
                        require(total == len(current["records"]), "V1 footer sample count mismatch")
                    decoded = dict(committed_samples=total, dropped_samples=lost, missed_deadlines=missed)
                current["footer"] = decoded
                if current["status"] != "damaged" and not current.get("_partial"):
                    current["status"] = "closed"
            except FormatError as exc:
                issue(offset + 15 * PAGE, "footer_invalid", str(exc), session=current)
    for session in result["sessions"]:
        if session["status"] == "interrupted":
            issue(session["sectors"][-1], "incomplete_session", "no fully verifiable clean session end", "warning")
        for key in tuple(session):
            if key.startswith("_"):
                del session[key]
    return result


def imu_csv(decoded):
    """Legacy RECORD READ columns/units, IMU only; does not imply firmware support."""
    output = io.StringIO(newline="")
    writer = csv.writer(output, lineterminator="\n")
    writer.writerow(["session", "timestamp_ms", "accel_x_g", "accel_y_g", "accel_z_g",
                     "gyro_x_dps", "gyro_y_dps", "gyro_z_dps"])
    for session in decoded["sessions"]:
        meta = session["metadata"]
        for record in session["records"]:
            if record["type"] != "IMU_RAW":
                continue
            ms = record.get("timestamp_ms")
            if ms is None:
                ms = (record["timestamp_us"] // 1000) & 0xFFFFFFFF
            scales = [meta["accel_ug_per_lsb"] / 1e6] * 3 + [meta["gyro_udps_per_lsb"] / 1e6] * 3
            writer.writerow([session["session_id"], ms] +
                            [f"{raw * scale:.6f}" for raw, scale in zip(record["axes_raw"], scales)])
    return output.getvalue()


def json_document(decoded):
    """JSON-safe wire representation: all u64 fields are decimal strings."""
    u64 = {"timestamp_us", "session_start_us", "gps_profile_checked_us", "stop_us",
           "first_record_us", "last_record_us", "missed_imu_intervals",
           "gps_checksum_failures", "invalid_rmc_observations", "detail"}
    arrays = {"committed_by_type", "dropped_by_type"}

    def convert(value):
        if isinstance(value, dict):
            return {key: ([str(n) for n in item] if key in arrays else
                          str(item) if key in u64 and item is not None else convert(item))
                    for key, item in value.items()}
        if isinstance(value, list):
            return [convert(item) for item in value]
        return value

    return convert(decoded)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--format", choices=("json", "imu-csv"), default="json")
    parser.add_argument("--output", type=Path, help="optional new host output file")
    args = parser.parse_args(argv)
    if args.output and args.output.resolve() == args.image.resolve():
        parser.error("output must not overwrite the input image")
    try:
        decoded = decode_image(args.image.read_bytes())
    except (OSError, FormatError) as exc:
        print(str(exc), file=sys.stderr)
        return 2
    output = json.dumps(json_document(decoded), indent=2, sort_keys=True) + "\n" if args.format == "json" else imu_csv(decoded)
    if args.output:
        try:
            with args.output.open("x", encoding="utf-8", newline="") as stream:
                stream.write(output)
        except OSError as exc:
            print(str(exc), file=sys.stderr)
            return 2
    else:
        sys.stdout.write(output)
    if args.format == "imu-csv" and decoded["issues"]:
        print(json.dumps(decoded["issues"], sort_keys=True), file=sys.stderr)
    return 2 if any(i["severity"] == "error" for i in decoded["issues"]) else 0


if __name__ == "__main__":
    raise SystemExit(main())
