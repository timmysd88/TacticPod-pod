"""Host-only lossless codec and experimental flash-container tests."""
from pathlib import Path
import math
import random
import struct
import sys
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.dont_write_bytecode = True
import storage_benchmark as b
from tacticpod_decode import decode_record


class StorageCompression(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.records = b.gps_rows(b.synthetic('walking', 1, 42), 'walking', 123)

    def check_all(self, records):
        for pi, predictor in enumerate(b.PREDICTORS):
            for bi, backend in enumerate(b.BACKENDS):
                with self.subTest(predictor=predictor, backend=backend):
                    frame = b.encode_block(records, pi, bi, 99)
                    seq, result = b.decode_block(frame)
                    self.assertEqual(seq, 99)
                    self.assertEqual(b.canonical(result), b.canonical(records))
                    self.assertLessEqual(len(frame), len(b.canonical(records)) + 36)

    def test_every_codec_and_backend_reconstructs_every_record(self):
        self.check_all(self.records)

    def test_all_movement_profiles_and_v2_validity(self):
        for name, blocks, _ in b.datasets(2, 2):
            with self.subTest(profile=name):
                # Full 200-sample blocks; no precision lost to normalisation.
                frame = b.encode_block(blocks[0], 8, 1)
                self.assertEqual(b.decode_block(frame)[1], blocks[0])
                for record in blocks[0]:
                    decode_record(record.wire())

    def test_real_csv_all_samples_and_partial_tail(self):
        samples, source = b.load_real(ROOT / 'logs/device-monitor-261027-204904.log')
        self.assertEqual(len(samples), 3026)
        self.assertTrue(source['csv_float32_roundtrip_verified'])
        self.assertEqual(samples[0].us, 70703000)
        self.assertEqual(samples[-1].us, 101575000)
        for at in range(0, len(samples), 100):
            records = b.gps_rows(samples[at:at + 100], 'real_recording', 123 + at, at)
            self.assertEqual(b.decode_block(b.encode_block(records, 8, 1))[1], records)
        self.assertEqual(len(samples[-26:]), 26)

    def test_signed_extrema_large_deltas_and_u64_rollover_domains(self):
        records = [b.Record(1, (1 << 32) - 1, (-32768,) * 6),
                   b.Record(1, 1 << 32, (32767,) * 6),
                   b.Record(1, (1 << 32) + 1, (-32768,) * 6),
                   b.Record(4, (1 << 64) - 4, (5, 1, 2, 3, 1, 0)),
                   b.Record(4, (1 << 64) - 3, (5, 1, 2, 3, 1, (1 << 64) - 1)),
                   b.Record(4, (1 << 64) - 2, (5, 1, 2, 3, 1, 0))]
        self.check_all(records)

    def test_invalid_fix_missing_utc_and_source_precision_unchanged(self):
        r = b.Record(2, 123456789, (0, 0, 0, 0, 0, 0, 0, ord('V'), 0, 0x4E47))
        self.check_all([r])
        self.assertFalse(decode_record(r.wire())['fix_valid'])

    def test_equal_timestamps_preserve_original_type_order(self):
        records = [b.Record(3, 1000, (0, 0, 0, 0, 0, 0, 0, 255, 0x4E47, 0)),
                   b.Record(1, 1000, (0,) * 6),
                   b.Record(2, 1000, (0, 0, 0, 0, 0, 0, 0, 0, 0, 0x4E47)),
                   b.Record(1, 1000, (1,) * 6)]
        self.check_all(records)

    def test_random_full_precision_samples_cannot_expand_beyond_raw_fallback(self):
        records = b.gps_rows(b.synthetic('adversarial_uniform', 2, 12), 'adversarial_uniform', 12)
        self.check_all(records)

    def test_zigzag_varints_and_reject_overlong(self):
        for n in (0, -1, 1, -32768, 65535, -65535, -(1 << 64), (1 << 64) - 1):
            encoded = b.varint(b.zigzag(n))
            reader = b.Reader(encoded)
            self.assertEqual(b.unzigzag(reader.varint()), n)
            reader.done()
        for encoded in (b'\x80' * 11, b'\x80\0', b'\x80'):
            with self.assertRaises(ValueError):
                b.Reader(encoded).varint()

    def test_bitpacking_zero_width_extremes_and_padding(self):
        for values in ([0] * 100, [1, 0, 1], [(1 << 66) - 1] * 3):
            width = max(values).bit_length()
            self.assertEqual(b.bitunpack(b.bitpack(values, width), len(values), width), values)
        with self.assertRaises(ValueError):
            b.bitunpack(b'\xff', 1, 1)

    def test_rice_escape_is_bounded_and_preserves_large_residuals(self):
        for dod in (False, True):
            values = [-32768, 32767, -32768, 0, 1, 1, 1]
            encoded = b.rice_column(values, 'h', dod)
            reader = b.Reader(encoded)
            self.assertEqual(b.unrice_column(reader, len(values), 'h', dod), values)
            reader.done()
        with self.assertRaises(ValueError):
            b.unrice_column(b.Reader(b'\0\0\xff\xff'), 2, 'h', False)

    def test_timestamp_gcd_and_run_encoding_are_exact_not_assumed(self):
        for times in ([0], [0, 0, 0], list(range(100)), [0, 10000, 20000, 30001, 40000, 50000],
                      [0, 10000, 20000, 32000, 42000, 52000, 62000]):
            encoded = b.adaptive_times(times)
            reader = b.Reader(encoded)
            self.assertEqual(b.inverse_times(reader, len(times)), times)
            reader.done()

    def test_lzss_empty_overlap_repeated_and_incompressible(self):
        rng = random.Random(9)
        for data in (b'', b'A', b'A' * 8192, bytes(range(256)) * 4,
                     bytes(rng.randrange(256) for _ in range(8192))):
            self.assertEqual(b.lzss_decode(b.lzss_encode(data), len(data)), data)

    def test_lzss_rejects_bad_distance_and_output_bombs(self):
        for encoded, expected in ((b'\0\0\0', 3), (b'\0\xff\xff', 18), (b'\x01A\0\0', 2)):
            with self.assertRaises(ValueError):
                b.lzss_decode(encoded, expected)

    def test_every_block_byte_corruption_detected(self):
        frame = b.encode_block(self.records, 8, 1)
        for at in range(len(frame)):
            damaged = bytearray(frame)
            damaged[at] ^= 1
            with self.subTest(byte=at):
                with self.assertRaises(ValueError):
                    b.decode_block(damaged)

    def test_truncation_and_crc_valid_length_corruption(self):
        frame = b.encode_block(self.records, 8, 1)
        for length in range(len(frame)):
            with self.assertRaises(ValueError):
                b.decode_block(frame[:length])
        for offset, replacement in ((5, 255), (6, 255), (7, 1), (12, 0), (16, 255)):
            damaged = bytearray(frame)
            damaged[offset] = replacement
            struct.pack_into('<I', damaged, len(damaged) - 4, zlib.crc32(damaged[:-4]))
            with self.assertRaises(ValueError):
                b.decode_block(damaged)

    def test_independently_decodable_blocks_and_exact_page_accounting(self):
        frames = [b.encode_block(self.records, 8, 1, i) for i in range(12)]
        for pages_per_sector in (14, 15):
            image, pages = b.page_image(frames, pages_per_sector)
            self.assertEqual(pages, sum(math.ceil(len(f) / 232) for f in frames))
            self.assertEqual(len(image), math.ceil(pages / pages_per_sector) * 4096)
            recovered, issues = b.recover(image, pages_per_sector)
            self.assertFalse(issues)
            self.assertEqual([b.canonical(r) for r in recovered], [b.canonical(self.records)] * 12)

    def test_power_cut_after_each_program_byte_preserves_prior_blocks(self):
        frames = [b.encode_block(self.records, 8, 1, i) for i in range(2)]
        image, _ = b.page_image(frames)
        # First block is committed. Tear every possible byte boundary of the
        # second block's first page, including its CRC and final marker.
        first_pages = math.ceil(len(frames[0]) / 232)
        page_at = (first_pages + 1) * 256
        for cut in range(256):
            damaged = bytearray(image)
            damaged[page_at + cut:page_at + 256] = b'\xff' * (256 - cut)
            recovered, _ = b.recover(damaged)
            self.assertEqual(len(recovered), 1)
            self.assertEqual(b.canonical(recovered[0]), b.canonical(self.records))

    def test_recovery_resynchronises_at_a_later_complete_block(self):
        frames = [b.encode_block(self.records, 8, 1, i) for i in range(20)]
        image, _ = b.page_image(frames)
        image = bytearray(image)
        image[4096 + 256 + 40] ^= 1
        recovered, issues = b.recover(image)
        self.assertTrue(issues)
        self.assertGreater(len(recovered), 2)
        self.assertLess(len(recovered), 20)
        self.assertTrue(all(b.canonical(r) == b.canonical(self.records) for r in recovered))

    def test_invalid_sector_header_never_exposes_unowned_records(self):
        image, _ = b.page_image([b.encode_block(self.records, 8, 1)])
        damaged = bytearray(image)
        damaged[0] ^= 1
        self.assertEqual(b.recover(damaged)[0], [])

    def test_exact_capacity_targets_and_20pct_safety_reserve(self):
        payload = 4095 * 14 * 232
        self.assertEqual(payload, 13300560)
        self.assertAlmostEqual(payload / 18000, 738.92)
        self.assertEqual(b.allocation([4], 18000)['data_pages'], 72000)
        self.assertFalse(b.allocation([4], 18000)['fits'])
        self.assertTrue(b.allocation([3], 18000)['fits'])
        self.assertEqual(b.duration([4]), 238.86666666666667)
        self.assertAlmostEqual(b.duration([7], block_seconds=2), 273.0)

    def test_stream_packing_and_sector_utilisation_are_exact(self):
        frames = [b.encode_block(self.records, 8, 1, i) for i in range(20)]
        for geometry in (14, 15):
            image, pages = b.stream_image(frames, geometry)
            self.assertEqual(pages, math.ceil(sum(map(len, frames)) / 232))
            self.assertEqual(len(image), math.ceil(pages / geometry) * 4096)
            recovered, issues = b.recover_stream(image, geometry)
            self.assertFalse(issues)
            self.assertEqual([b.canonical(r) for r in recovered], [b.canonical(self.records)] * 20)
        self.assertLessEqual(b.stream_allocation(list(map(len, frames)), 18000)['data_pages'],
                             b.allocation([math.ceil(len(f) / 232) for f in frames], 18000)['data_pages'])

    def test_stream_power_cut_each_byte_and_independent_resynchronisation(self):
        frames = [b.encode_block(self.records, 8, 1, i) for i in range(20)]
        image, _ = b.stream_image(frames)
        # Corrupt a page after the first complete frame, then recover later
        # sectors using their explicit first-block-start offset.
        page_index = math.ceil(len(frames[0]) / 232)
        at = (page_index + 1) * 256
        for cut in range(256):
            damaged = bytearray(image)
            damaged[at + cut:at + 256] = b'\xff' * (256 - cut)
            recovered, issues = b.recover_stream(damaged)
            self.assertTrue(issues)
            self.assertGreater(len(recovered), 1)
            self.assertLess(len(recovered), 20)
            self.assertTrue(all(b.canonical(r) == b.canonical(self.records) for r in recovered))

    def test_stream_valid_crc_invalid_page_envelope_is_rejected(self):
        frame = b.encode_block(self.records, 8, 1)
        image, _ = b.stream_image([frame])
        damaged = bytearray(image)
        struct.pack_into('<H', damaged, 256 + 12, 233)
        struct.pack_into('<I', damaged, 256 + 248, zlib.crc32(damaged[256:256 + 248]))
        self.assertEqual(b.recover_stream(damaged)[0], [])

    def test_stream_capacity_includes_final_frame_and_diagnostic_sector(self):
        allocated = b.stream_allocation([1000], 18000, 15)
        self.assertEqual(allocated['data_pages'], math.ceil((18000000 + 256) / 232))
        self.assertEqual(allocated['chip_bytes_with_diagnostic'], allocated['recording_bytes'] + 4096)
        self.assertFalse(allocated['fits'])

    def test_block_size_comparison_uses_identical_source_bytes(self):
        one = {name: b''.join(b.canonical(block) for block in blocks)
               for name, blocks, _ in b.datasets(2, 1)}
        two = {name: b''.join(b.canonical(block) for block in blocks)
               for name, blocks, _ in b.datasets(2, 2)}
        self.assertEqual(one, two)

    def test_crc_valid_invalid_deflate_is_rejected_without_unbounded_output(self):
        valid = b.encode_block(self.records, 0, 3)
        header = list(b.HEADER.unpack(valid[:32]))
        header[3] = 3
        body = struct.pack('<H', 8192) + b'\xff\xff\xff\xff'
        header[7] = len(body)
        damaged = b.HEADER.pack(*header) + body
        damaged += struct.pack('<I', zlib.crc32(damaged))
        with self.assertRaises(ValueError):
            b.decode_block(damaged)


if __name__ == '__main__':
    unittest.main(verbosity=2)
