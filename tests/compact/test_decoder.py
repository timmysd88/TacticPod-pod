"""Host-only compact v3 malformed inputs; no serial/device dependencies."""
from pathlib import Path
import random
import struct
import sys
import unittest
import zlib

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
sys.dont_write_bytecode=True
import storage_benchmark as b
from compact_decode import decode_frame,decode_stream


def frame(records,predictor=8,sequence=0):
    base=records[0].us;raw=b.canonical(records)
    counts=[sum(r.kind==k for r in records) for k in range(1,5)]
    body=b.transform(records,predictor,base)
    data=b.HEADER.pack(b'TCB3',0,predictor,0,0,sequence,len(raw),len(body),*counts,base)+body
    return data+struct.pack('<I',zlib.crc32(data))


def replace(data,at,payload):
    out=bytearray(data);out[at:at+len(payload)]=payload
    out[-4:]=struct.pack('<I',zlib.crc32(out[:-4]));return out


class CompactDecoder(unittest.TestCase):
    def setUp(self):
        self.records=[b.Record(1,2**32+7+i*10000,(i,-i,-32768,32767,0,123)) for i in range(20)]
        self.frame=frame(self.records)

    def test_golden_fixture(self):
        fixtures=Path(__file__).with_name('fixtures')
        self.assertEqual(decode_frame((fixtures/'stillness.tcb3').read_bytes())['raw'],
                         (fixtures/'stillness.v2wire').read_bytes())

    def test_exact_all_column_modes(self):
        for mode in range(7):
            rows=[b.Record(1,1000+i*10000,(i*i,-i,32767-i,-32768+i,7,0)) for i in range(20)]
            order=b.bitpack([0]*len(rows),2);body=bytearray(order+b.adaptive_times([r.us-1000 for r in rows]))
            for column,fmt in enumerate(b.COLUMNS[1]):
                values=[r.values[column] for r in rows]
                encoded=(b''.join(struct.pack('<'+fmt,v) for v in values) if mode==0 else
                         b.encode_column(values,fmt,mode in (2,4),mode in (3,4)) if mode<5 else
                         b.rice_column(values,fmt,mode==6))
                body.extend(bytes([mode])+encoded)
            raw=b.canonical(rows)
            data=b.HEADER.pack(b'TCB3',0,8,0,0,0,len(raw),len(body),20,0,0,0,1000)+body
            self.assertEqual(decode_frame(data+struct.pack('<I',zlib.crc32(data)))['raw'],raw)

    def test_exact_timestamp_modes(self):
        for mode in range(4):
            # Force the requested wire mode, independently of size selection.
            rows=[b.Record(1,1000+i*10000,(0,)*6) for i in range(20)]
            if mode==0:times=b'\0'+b.varint(0)+b.varint(10000)*19
            elif mode==1:times=b'\1'+b.varint(0)+b.varint(b.zigzag(10000))+b'\0'*18
            elif mode==2:times=b'\2'+b.varint(0)+b.varint(10000)+b.varint(2)+b'\0'*18
            else:times=b'\3'+b.varint(0)+b.varint(10000)+b'\0'+b.varint(18)
            body=b.bitpack([0]*20,2)+times+b'\0'+b'\0'*40
            body+= (b'\0'+b'\0'*40)*5
            data=b.HEADER.pack(b'TCB3',0,8,0,0,0,480,len(body),20,0,0,0,1000)+body
            self.assertEqual(decode_frame(data+struct.pack('<I',zlib.crc32(data)))['raw'],b.canonical(rows))

    def test_raw_fallback(self):
        self.assertEqual(decode_frame(frame(self.records,0))['raw'],b.canonical(self.records))

    def test_bad_header_with_valid_crc(self):
        for at,value in ((0,b'TCB0'),(4,b'\1'),(5,b'\1'),(6,b'\1'),(7,b'\1'),
                         (12,b'\0\0'),(14,b'\0\0'),(16,b'\xff\xff'),(24,b'\xff'*8)):
            with self.subTest(at=at),self.assertRaises(ValueError):decode_frame(replace(self.frame,at,value))

    def test_raw_wrong_type_counts(self):
        data=frame(self.records,0)
        with self.assertRaises(ValueError):decode_frame(replace(data,16,struct.pack('<4H',19,1,0,0)))

    def test_interrupted_every_byte(self):
        for n in range(1,len(self.frame)):
            with self.assertRaises(ValueError):decode_stream(self.frame[:n])

    def test_corrupt_every_byte(self):
        for n in range(len(self.frame)):
            damaged=bytearray(self.frame);damaged[n]^=1
            with self.assertRaises(ValueError):decode_frame(damaged)

    def test_backwards_sequence(self):
        with self.assertRaises(ValueError):decode_stream(self.frame+self.frame)

    def test_independent_after_interruption(self):
        # Recovery can discard a damaged frame and decode the next whole block.
        second=frame(self.records,sequence=5)
        self.assertEqual(decode_frame(second)['raw'],b.canonical(self.records))
        self.assertEqual(len(decode_stream(self.frame+second)),2)

    def test_timestamp_window(self):
        with self.assertRaises(ValueError):decode_frame(frame([self.records[0],b.Record(1,self.records[0].us+2000000,(0,)*6)]))

    def test_bad_order_and_modes(self):
        for at,payload in ((32,b'\xff'),(37,b'\xff')):
            with self.assertRaises(ValueError):decode_frame(replace(self.frame,at,payload))

    def test_deterministic_crc_valid_fuzz(self):
        rng=random.Random(303)
        for _ in range(1000):
            at=rng.randrange(32,len(self.frame)-4)
            damaged=replace(self.frame,at,bytes([rng.randrange(256)]))
            try:decoded=decode_frame(damaged)
            except ValueError:continue
            self.assertEqual(len(decoded['raw']),480)


if __name__=='__main__':unittest.main()
