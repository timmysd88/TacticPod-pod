"""Host-only V1/V2 conformance tests. No compiler, serial port, or device needed."""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import tacticpod_decode as decoder
import generate_fixtures as golden


class Conformance(unittest.TestCase):
    def setUp(self):
        self.base = bytearray((golden.HERE / "missing_gps.bin").read_bytes())

    @staticmethod
    def fixture_bytes(path):
        content = path.read_bytes()
        return content.replace(b"\r\n", b"\n") if path.suffix == ".json" else content

    @staticmethod
    def repaired(image, page_at=256):
        image = bytearray(image)
        struct.pack_into("<I", image, page_at + 248, zlib.crc32(image[page_at:page_at + 248]))
        return bytes(image)

    def rejected(self, image, message):
        out = decoder.decode_image(bytes(image))
        self.assertTrue(any(message in i["message"] and i["severity"] == "error" for i in out["issues"]), out)
        return out

    def test_all_independent_golden_outputs(self):
        for expected in sorted(golden.HERE.glob("*.expected.json")):
            with self.subTest(fixture=expected.stem):
                image = expected.with_name(expected.name.replace(".expected.json", ".bin"))
                self.assertEqual(decoder.json_document(decoder.decode_image(image.read_bytes())), json.loads(expected.read_text()))

    def test_fixture_regeneration_is_byte_identical(self):
        for name, content in golden.generate().items():
            with self.subTest(file=name):
                self.assertEqual(self.fixture_bytes(golden.HERE / name), content)

    def test_fixture_sha256(self):
        manifest = json.loads((golden.HERE / "sha256.json").read_text())
        for name, sha in manifest.items():
            self.assertEqual(hashlib.sha256(self.fixture_bytes(golden.HERE / name)).hexdigest(), sha)

    def test_literal_record_and_header_bytes(self):
        image = (golden.HERE / "mixed_v1_v2.bin").read_bytes()
        # Independent little-endian signed axes and a u64 near micros wrap.
        self.assertEqual(image[4096 + 256 + 16:4096 + 256 + 40].hex(),
                         "01011800faffffff00000000000000000000ffff01000200")
        header = image[4096:4352]
        self.assertEqual(header[:16].hex(), "5450483202000000000000000000c400")
        self.assertEqual(header[16:24].hex(), "0200000000000000")
        self.assertEqual(header[104:112], bytes.fromhex("a086010040960100"))
        self.assertEqual(header[208:212], b"\0" * 4)
        self.assertEqual(header[212:248], b"\xff" * 36)
        self.assertEqual(header[252:], bytes.fromhex("dec0dec0"))

    def test_crc_standard_vector_and_every_body_byte(self):
        self.assertEqual(zlib.crc32(b"123456789"), 0xCBF43926)
        for n in range(248):
            image = self.base.copy()
            image[256 + n] ^= 1
            with self.subTest(byte=n):
                self.rejected(image, "CRC mismatch")

    def test_bad_crc_word_and_commit_marker(self):
        image = self.base.copy()
        image[256 + 248] ^= 1
        self.rejected(image, "CRC mismatch")
        image = self.base.copy()
        image[256 + 252] ^= 1
        out = decoder.decode_image(image)
        self.assertEqual(out["sessions"][0]["records"], [])
        self.assertTrue(any(i["message"] == "missing commit marker" for i in out["issues"]))

    def test_lengths_counts_and_padding(self):
        for relative, value, fmt, message in (
                (14, 233, "H", "envelope"), (12, 2, "H", "record count"),
                (18, 0, "H", "record exceeds"), (18, 45, "H", "record exceeds"),
                (18, 40, "H", "wrong known-record size")):
            image = self.base.copy()
            struct.pack_into("<" + fmt, image, 256 + relative, value)
            self.rejected(self.repaired(image), message)
        image = self.base.copy()
        image[256 + 100] = 0
        self.rejected(self.repaired(image), "padding")

    def test_crc_valid_invalid_page_exports_no_partial_records(self):
        first = golden.imu(4294967290, (1, 2, 3, 4, 5, 6))
        second = golden.rmc(4294967300)
        image = bytearray(golden.sector(golden.header(), [golden.data([first, second])]))
        # Second record's latitude is out of range, but page CRC is valid.
        struct.pack_into("<i", image, 256 + 16 + 24 + 16, 900000001)
        out = self.rejected(self.repaired(image), "coordinates")
        self.assertEqual(out["sessions"][0]["records"], [])

    def test_valid_and_void_positions_are_distinct(self):
        out = decoder.decode_image((golden.HERE / "mixed_v1_v2.bin").read_bytes())
        records = [r for r in out["sessions"][1]["records"] if r["type"] == "GPS_RMC"]
        self.assertTrue(records[0]["fix_valid"])
        self.assertEqual(records[0]["position"]["longitude_deg"], -0.090003)
        self.assertFalse(records[1]["fix_valid"])
        self.assertIsNone(records[1]["position"])
        self.assertIsNone(records[1]["speed_m_s"])
        self.assertEqual(records[1]["fields"]["latitude_e7"], 507752560)

    def test_zero_coordinates_and_zero_speed_are_valid(self):
        raw = golden.record(2, 4294967290, golden.pack("IiiIIIIBB2s", 15, 0, 0, 0, 0, 0, 0, 65, 0, b"GN"))
        r = decoder.decode_record(raw)
        self.assertEqual(r["position"], dict(latitude_deg=0.0, longitude_deg=0.0))
        self.assertEqual(r["speed_m_s"], 0)
        self.assertIsNone(r["utc"]["date"])

    def test_field_masks_ranges_and_dates(self):
        raw = bytearray(golden.rmc(4294967300)[0])
        for offset, fmt, value, message in (
                (12, "I", 0x80, "flags/status"),
                (16, "i", -900000001, "coordinates"),
                (20, "i", 1800000001, "coordinates"),
                (28, "I", 36000000, "course"),
                (32, "I", 20260230, "UTC date"),
                (36, "I", 86400000, "UTC time"),
                (40, "B", 86, "RMC A")):
            data = raw.copy()
            struct.pack_into("<" + fmt, data, offset, value)
            with self.subTest(message=message):
                with self.assertRaisesRegex(decoder.FormatError, message):
                    decoder.decode_record(data)
        data = raw.copy()
        struct.pack_into("<I", data, 12, 123)  # speed unavailable but stored nonzero
        with self.assertRaisesRegex(decoder.FormatError, "speed must be zero"):
            decoder.decode_record(data)

    def test_quality_is_not_accuracy(self):
        r = decoder.decode_record(golden.gga(4294967300)[0])
        self.assertEqual(r["hdop"], 1.28)
        self.assertEqual(r["altitude_msl_mm"], -1200)
        self.assertNotIn("horizontal_accuracy_m", r)
        self.assertNotIn("fix_valid", r)
        data = bytearray(golden.gga(4294967300)[0])
        struct.pack_into("<I", data, 12, 62)
        with self.assertRaisesRegex(decoder.FormatError, "date requires UTC time"):
            decoder.decode_record(data)

    def test_rmc_mode_n_and_status_a_do_not_create_valid_fix(self):
        raw = bytearray(golden.rmc(4294967300)[0])
        raw[41] = ord("N")
        with self.assertRaisesRegex(decoder.FormatError, "fix flag"):
            decoder.decode_record(raw)
        struct.pack_into("<I", raw, 12, 125)
        decoded = decoder.decode_record(raw)
        self.assertFalse(decoded["fix_valid"])
        self.assertIsNone(decoded["position"])

    def test_json_u64_fields_are_lossless_decimal_strings(self):
        out = decoder.decode_image((golden.HERE / "mixed_v1_v2.bin").read_bytes())
        safe = decoder.json_document(out)
        self.assertEqual(safe["sessions"][1]["records"][-1]["detail"], "18446744073709551614")
        self.assertEqual(safe["sessions"][1]["footer"]["committed_by_type"], ["1", "2", "1", "1"])
        self.assertEqual(safe["sessions"][1]["metadata"]["session_start_us"], "4294967200")
        self.assertIsInstance(out["sessions"][1]["records"][-1]["detail"], int)

    def test_monotonic_reserved_value_rejected(self):
        raw = bytearray(golden.imu(4294967300, (1, 2, 3, 4, 5, 6))[0])
        struct.pack_into("<Q", raw, 4, (1 << 64) - 1)
        with self.assertRaisesRegex(decoder.FormatError, "reserved timestamp"):
            decoder.decode_record(raw)

    def test_footer_invalid_fix_counter_is_checked(self):
        image = self.base.copy()
        struct.pack_into("<Q", image, 3840 + 112, 0)
        self.rejected(self.repaired(image, 3840), "invalid-fix count mismatch")

    def test_unsupported_continuation_metadata_is_not_merged(self):
        image = bytearray((golden.HERE / "mixed_v1_v2.bin").read_bytes())
        struct.pack_into("<I", image, 8192 + 104, 50000)
        out = decoder.decode_image(self.repaired(image, 8192))
        self.assertEqual(len(out["sessions"]), 3)
        self.assertEqual(out["sessions"][-1]["status"], "damaged")
        self.assertTrue(any(i["code"] == "missing_prefix" for i in out["issues"]))
        self.assertTrue(any(i["code"] == "continuation_invalid" for i in out["issues"]))

    def test_u64_rollovers_and_large_event_detail(self):
        mixed = decoder.decode_image((golden.HERE / "mixed_v1_v2.bin").read_bytes())
        r = mixed["sessions"][1]["records"]
        self.assertEqual(r[0]["timestamp_us"], 0xFFFFFFFA)
        self.assertEqual(r[1]["timestamp_us"], 0x100000004)
        self.assertEqual(r[-1]["detail"], (1 << 64) - 2)
        rolled = decoder.decode_image((golden.HERE / "timestamp_rollover.bin").read_bytes())
        times = [r["timestamp_us"] for r in rolled["sessions"][0]["records"]]
        self.assertEqual(times[1] - times[0], 2000)

    def test_v1_csv_preserves_raw_wrap_and_v2_imu_only(self):
        out = decoder.decode_image((golden.HERE / "mixed_v1_v2.bin").read_bytes())
        csv = decoder.imu_csv(out).splitlines()
        self.assertEqual(csv[0], "session,timestamp_ms,accel_x_g,accel_y_g,accel_z_g,gyro_x_dps,gyro_y_dps,gyro_z_dps")
        self.assertEqual(len(csv), 4)
        self.assertEqual(csv[1], "1,4294967280,0.000061,-0.000122,1.998787,-286.720000,0.000000,0.875000")
        self.assertTrue(csv[2].startswith("1,16,"))
        self.assertTrue(csv[3].startswith("2,4294967,"))

    def test_v1_new_scale_metadata_and_csv(self):
        image = bytearray((golden.HERE / "mixed_v1_v2.bin").read_bytes())
        struct.pack_into("<II", image, 24, 244, 35000)
        out = decoder.decode_image(self.repaired(image, 0))
        session = next(s for s in out["sessions"] if s["version"] == 1)
        self.assertEqual(session["metadata"]["accel_ug_per_lsb"], 244)
        self.assertEqual(session["metadata"]["gyro_udps_per_lsb"], 35000)
        row = decoder.imu_csv(out).splitlines()[1]
        self.assertEqual(row, "1,4294967280,0.000244,-0.000488,7.995148,-1146.880000,0.000000,3.500000")

    def test_v2_legacy_csv_low32_milliseconds(self):
        out = decoder.decode_image((golden.HERE / "timestamp_rollover.bin").read_bytes())
        rows = decoder.imu_csv(out).splitlines()
        self.assertTrue(rows[1].startswith("2,4294967295,"))
        self.assertTrue(rows[2].startswith("2,1,"))

    def test_unknown_optional_record_and_schema_skip(self):
        for kind, version in ((90, 1), (2, 7)):
            raw = struct.pack("<BBHQ", kind, version, 15, 4294967290) + b"abc"
            image = golden.sector(golden.header(), [golden.page(b"TPD2", 2, 0, 1, raw)])
            out = decoder.decode_image(image)
            r = out["sessions"][0]["records"][0]
            self.assertEqual(r["type"], "UNKNOWN")
            self.assertEqual(r["raw_hex"], "616263")

    def test_full_metadata_fits_exactly_and_unknown_optional_tlv(self):
        h = bytearray(golden.header())
        struct.pack_into("<H", h, 14, 232)
        struct.pack_into("<H", h, 208, 36)
        h[212:248] = struct.pack("<HH", 42, 32) + bytes(range(32))
        sid, seq, index, meta = decoder.header_v2(self.repaired(h, 0))
        self.assertEqual((sid, seq, index), (2, 0, 0))
        self.assertEqual(meta["extensions"], [dict(tag=42, raw_hex=bytes(range(32)).hex())])
        struct.pack_into("<H", h, 212, 0x8001)
        with self.assertRaisesRegex(decoder.FormatError, "mandatory metadata TLV"):
            decoder.header_v2(self.repaired(h, 0))

    def test_bad_metadata(self):
        for at, fmt, value, message in (
                (16, "H", 3, "version"), (18, "H", 1, "version"),
                (140, "I", 63, "feature"), (112, "I", 0, "scale/rate"),
                (153, "B", 2, "timestamp"), (208, "H", 37, "TLV"),
                (210, "H", 1, "reserved")):
            image = self.base.copy()
            struct.pack_into("<" + fmt, image, at, value)
            self.rejected(self.repaired(image, 0), message)

    def test_unknown_profile_and_missing_identity(self):
        h = bytearray(golden.header())
        h[152] = 0
        struct.pack_into("<Q", h, 144, golden.MAX64)
        self.assertIsNone(decoder.header_v2(self.repaired(h, 0))[3]["gps_profile_checked_us"])
        h[32:40] = b"\0" * 8
        with self.assertRaisesRegex(decoder.FormatError, "identity/hash"):
            decoder.header_v2(self.repaired(h, 0))

    def test_sequence_session_and_timestamp_rejection(self):
        for at, fmt, value, message in ((260, "I", 999, "session/sequence"),
                                         (264, "I", 1, "session/sequence"),
                                         (276, "Q", 1, "backwards monotonic")):
            image = self.base.copy()
            struct.pack_into("<" + fmt, image, at, value)
            self.rejected(self.repaired(image), message)

    def test_footer_count_timestamp_and_crc_rejection(self):
        for at, value, message in ((3840 + 32, 2, "counts mismatch"),
                                    (3840 + 128, 4294967280, "timestamp")):
            image = self.base.copy()
            struct.pack_into("<Q", image, at, value)
            self.rejected(self.repaired(image, 3840), message)
        image = self.base.copy()
        image[3840 + 50] ^= 1
        self.rejected(image, "CRC mismatch")

    def test_later_committed_sector_survives_bad_page(self):
        image = bytearray((golden.HERE / "mixed_v1_v2.bin").read_bytes())
        image[4096 + 256 + 40] ^= 1
        out = decoder.decode_image(image)
        self.assertEqual(out["sessions"][1]["status"], "damaged")
        self.assertEqual([r["type"] for r in out["sessions"][1]["records"]], ["GPS_RMC", "EVENT"])
        self.assertTrue(any(i["code"] == "sequence_gap" for i in out["issues"]))

    def test_missing_header_never_recovers_unowned_payload(self):
        image = self.base.copy()
        image[:256] = b"\xff" * 256
        out = decoder.decode_image(image)
        self.assertEqual(out["sessions"], [])
        self.assertEqual(out["issues"][0]["code"], "unknown_header")

    def test_blank_and_unknown_version_sectors(self):
        self.assertEqual(decoder.decode_image(b"\xff" * 4096), dict(schema="tacticpod.decode/1", sessions=[], issues=[]))
        image = self.base.copy()
        image[3] = ord("9")
        self.assertEqual(decoder.decode_image(image)["sessions"], [])

    def test_partial_dump_and_reserved_sector(self):
        out = decoder.decode_image(bytes(self.base) + b"torn")
        self.assertEqual(out["issues"][0]["code"], "truncated_sector")
        full = bytes(self.base) + b"\xff" * (0xFFF000 - len(self.base)) + b"\0" * 4096
        self.assertEqual(decoder.decode_image(full), decoder.decode_image(self.base))
        with self.assertRaisesRegex(decoder.FormatError, "physical flash"):
            decoder.decode_image(full + b"x")

    def test_nonblank_tail_is_not_silently_ignored(self):
        image = self.base.copy()
        image[3 * 256] = 0
        self.rejected(image, "data after first blank")

    def test_cli_json_csv_exit_codes_and_input_preservation(self):
        script = ROOT / "tools/tacticpod_decode.py"
        valid = golden.HERE / "mixed_v1_v2.bin"
        for name, expected_exit in (("mixed_v1_v2", 0), ("interrupted", 0), ("corrupt_crc", 2)):
            proc = subprocess.run([sys.executable, str(script), str(golden.HERE / (name + ".bin"))], capture_output=True, text=True)
            self.assertEqual(proc.returncode, expected_exit)
            self.assertEqual(json.loads(proc.stdout), json.loads((golden.HERE / (name + ".expected.json")).read_text()))
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "imu.csv"
            proc = subprocess.run([sys.executable, str(script), str(valid), "--format", "imu-csv", "--output", str(output)], capture_output=True)
            self.assertEqual(proc.returncode, 0)
            self.assertEqual(output.read_text(), decoder.imu_csv(decoder.decode_image(valid.read_bytes())))
            existing = output.read_bytes()
            proc = subprocess.run([sys.executable, str(script), str(valid), "--output", str(output)], capture_output=True)
            self.assertEqual(proc.returncode, 2)
            self.assertEqual(output.read_bytes(), existing)
            before = valid.read_bytes()
            proc = subprocess.run([sys.executable, str(script), str(valid), "--output", str(valid)], capture_output=True)
            self.assertEqual(proc.returncode, 2)
            self.assertEqual(valid.read_bytes(), before)


if __name__ == "__main__":
    unittest.main(verbosity=2)
