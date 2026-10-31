"""Host-only, exact-round-trip storage experiment. Never accesses a device.

Experimental TCB0 blocks are NOT frozen V2 pages or a firmware implementation.
Python 3.10+, standard library. Synthetic signals are illustrative, not validated
biomechanical models. Timing measurements are host timings, NOT nRF52840 timings.
"""
from __future__ import annotations
import argparse
from collections import defaultdict
import csv
from dataclasses import dataclass
import hashlib
import io
import json
import math
import platform
from pathlib import Path
import random
import statistics
import struct
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
CAPACITY = 0xFFF000
PAYLOAD = 232
HEADER = struct.Struct('<4sBBBBIHH4HQ')
FORMATS = {1: '<6h', 2: '<IiiIIIIBBH', 3: '<IIIiiHHBHB', 4: '<HBBIIQ'}
COLUMNS = {1: ['h'] * 6, 2: list('IiiIIIIBBH'), 3: list('IIIiiHHBHB'), 4: list('HBBIIQ')}
LENGTHS = {kind: struct.calcsize(fmt) + 12 for kind, fmt in FORMATS.items()}
PREDICTORS = ('v2_raw', 'block_u32', 'time_delta', 'time_dod', 'delta_varint',
              'dod_varint', 'delta_bitpack', 'dod_bitpack', 'adaptive_rice')
BACKENDS = ('none', 'lzss1k', 'deflate512', 'deflate32k')
MAX_BODY = 8192


@dataclass(frozen=True)
class Record:
    kind: int
    us: int
    values: tuple[int, ...]

    def wire(self):
        return struct.pack('<BBHQ', self.kind, 1, LENGTHS[self.kind], self.us) + struct.pack(FORMATS[self.kind], *self.values)


def canonical(records):
    return b''.join(r.wire() for r in records)


def zigzag(n):
    return 2 * n if n >= 0 else -2 * n - 1


def unzigzag(n):
    return -(n // 2) - 1 if n & 1 else n // 2


def varint(n):
    if not 0 <= n < 1 << 70:
        raise ValueError('varint range')
    out = bytearray()
    while n >= 128:
        out.append((n & 127) | 128)
        n >>= 7
    out.append(n)
    return out


class Reader:
    def __init__(self, data):
        self.data, self.at = data, 0

    def read(self, count):
        if count < 0 or self.at + count > len(self.data):
            raise ValueError('truncated input')
        out = self.data[self.at:self.at + count]
        self.at += count
        return out

    def number(self, fmt):
        return struct.unpack('<' + fmt, self.read(struct.calcsize('<' + fmt)))[0]

    def varint(self):
        value = 0
        for n in range(10):
            byte = self.number('B')
            value |= (byte & 127) << (n * 7)
            if not byte & 128:
                if n and not byte:
                    raise ValueError('noncanonical varint')
                return value
        raise ValueError('unbounded varint')

    def done(self):
        if self.at != len(self.data):
            raise ValueError('trailing bytes')


def bitpack(values, width):
    out, bits, pending = bytearray(), 0, 0
    for value in values:
        bits |= value << pending
        pending += width
        while pending >= 8:
            out.append(bits & 255)
            bits >>= 8
            pending -= 8
    if pending:
        out.append(bits)
    return out


def bitunpack(data, count, width):
    if width > 66 or len(data) != (count * width + 7) // 8:
        raise ValueError('bit width/length')
    out, pending, bits = [], 0, 0
    source = iter(data)
    for _ in range(count):
        while pending < width:
            bits |= next(source) << pending
            pending += 8
        out.append(bits & ((1 << width) - 1))
        bits >>= width
        pending -= width
    if bits:
        raise ValueError('nonzero bit padding')
    return out


def encode_column(values, fmt, dod, packed):
    seed_count = min(len(values), 2 if dod else 1)
    out = bytearray(b''.join(struct.pack('<' + fmt, v) for v in values[:seed_count]))
    residuals = [zigzag(values[i] - values[i - 1] -
                        (values[i - 1] - values[i - 2] if dod else 0))
                 for i in range(seed_count, len(values))]
    if packed:
        width = max(residuals, default=0).bit_length()
        out.append(width)
        out.extend(bitpack(residuals, width))
    else:
        for n in residuals:
            out.extend(varint(n))
    return out


def decode_column(reader, count, fmt, dod, packed):
    seed_count = min(count, 2 if dod else 1)
    values = [reader.number(fmt) for _ in range(seed_count)]
    left = count - seed_count
    if packed:
        width = reader.number('B')
        if width > 66:
            raise ValueError('unbounded bit width')
        residuals = bitunpack(reader.read((left * width + 7) // 8), left, width)
    else:
        residuals = [reader.varint() for _ in range(left)]
    for residual in residuals:
        values.append(values[-1] + unzigzag(residual) + (values[-1] - values[-2] if dod else 0))
        # Reject arithmetic overflow instead of allowing a wrapped raw sample.
        try:
            struct.pack('<' + fmt, values[-1])
        except struct.error as exc:
            raise ValueError('decoded field overflow') from exc
    return values


class Bits:
    def __init__(self, reader=None):
        self.bits, self.pending, self.output, self.reader = 0, 0, bytearray(), reader

    def write(self, value, width):
        self.bits |= value << self.pending
        self.pending += width
        while self.pending >= 8:
            self.output.append(self.bits & 255)
            self.bits >>= 8
            self.pending -= 8

    def finish(self):
        if self.pending:
            self.output.append(self.bits)
        return self.output

    def read(self, width):
        while self.pending < width:
            self.bits |= self.reader.number('B') << self.pending
            self.pending += 8
        value = self.bits & ((1 << width) - 1)
        self.bits >>= width
        self.pending -= width
        return value


def rice_column(values, fmt, dod):
    seed = min(len(values), 2 if dod else 1)
    residuals = [zigzag(values[i] - values[i - 1] - (values[i - 1] - values[i - 2] if dod else 0))
                 for i in range(seed, len(values))]
    width = max(residuals, default=0).bit_length()
    k = min(range(17), key=lambda k: sum((n >> k) + 1 + k if n >> k < 31 else 31 + width for n in residuals))
    output = bytearray(b''.join(struct.pack('<' + fmt, n) for n in values[:seed]))
    output.extend((k, width))
    bits = Bits()
    for n in residuals:
        quotient = n >> k
        if quotient >= 31:
            bits.write((1 << 31) - 1, 31)
            bits.write(n, width)
        else:
            bits.write((1 << quotient) - 1, quotient)
            bits.write(0, 1)
            bits.write(n & ((1 << k) - 1), k)
    output.extend(bits.finish())
    return output


def unrice_column(reader, count, fmt, dod):
    seed = min(count, 2 if dod else 1)
    values = [reader.number(fmt) for _ in range(seed)]
    k, width = reader.number('B'), reader.number('B')
    if k > 16 or width > 66:
        raise ValueError('Rice parameter bound')
    bits = Bits(reader)
    for _ in range(count - seed):
        quotient = 0
        while quotient < 31 and bits.read(1):
            quotient += 1
        residual = bits.read(width) if quotient == 31 else (quotient << k) | bits.read(k)
        values.append(values[-1] + unzigzag(residual) + (values[-1] - values[-2] if dod else 0))
        try:
            struct.pack('<' + fmt, values[-1])
        except struct.error as exc:
            raise ValueError('Rice field overflow') from exc
    if bits.bits:
        raise ValueError('nonzero Rice padding')
    return values


def adaptive_column(values, fmt):
    candidates = [b''.join(struct.pack('<' + fmt, n) for n in values),
                  encode_column(values, fmt, False, False), encode_column(values, fmt, True, False),
                  encode_column(values, fmt, False, True), encode_column(values, fmt, True, True),
                  rice_column(values, fmt, False), rice_column(values, fmt, True)]
    mode = min(range(len(candidates)), key=lambda n: len(candidates[n]))
    return bytes([mode]) + candidates[mode]


def inverse_adaptive(reader, count, fmt):
    mode = reader.number('B')
    if mode == 0:
        return [reader.number(fmt) for _ in range(count)]
    if mode in (1, 2, 3, 4):
        return decode_column(reader, count, fmt, mode in (2, 4), mode in (3, 4))
    if mode in (5, 6):
        return unrice_column(reader, count, fmt, mode == 6)
    raise ValueError('unknown column mode')


def adaptive_times(offsets):
    candidates = []
    delta = [b - a for a, b in zip(offsets, offsets[1:])]
    for mode in range(3):
        quantum = max(1, math.gcd(*delta)) if mode == 2 else 1
        out = bytearray(varint(offsets[0]))
        if mode == 2:
            out.extend(varint(quantum))
        previous = 0
        for gap in delta:
            gap //= quantum
            out.extend(varint(gap if mode == 0 else zigzag(gap - previous)))
            previous = gap
        candidates.append(out)
    # Zero residual runs, with exact first delta; no assumed acquisition times.
    out = bytearray(varint(offsets[0]))
    if delta:
        out.extend(varint(delta[0]))
        residuals = [b - a for a, b in zip(delta, delta[1:])]
        index = 0
        while index < len(residuals):
            if residuals[index] == 0:
                end = index + 1
                while end < len(residuals) and residuals[end] == 0:
                    end += 1
                out.extend(varint(0)); out.extend(varint(end - index)); index = end
            else:
                out.extend(varint(zigzag(residuals[index]) + 1)); index += 1
    candidates.append(out)
    mode = min(range(4), key=lambda n: len(candidates[n]))
    return bytes([mode]) + candidates[mode]


def inverse_times(reader, count):
    mode = reader.number('B')
    if mode > 3:
        raise ValueError('timestamp mode')
    offsets = [reader.varint()]
    quantum = reader.varint() if mode == 2 else 1
    if not quantum:
        raise ValueError('zero timestamp quantum')
    if mode == 3:
        if count > 1:
            previous = reader.varint()
            offsets.append(offsets[0] + previous)
        while len(offsets) < count:
            token = reader.varint()
            if token == 0:
                run = reader.varint()
                if not 0 < run <= count - len(offsets):
                    raise ValueError('timestamp run bound')
                for _ in range(run):
                    offsets.append(offsets[-1] + previous)
            else:
                previous += unzigzag(token - 1)
                offsets.append(offsets[-1] + previous)
    else:
        previous = 0
        for _ in range(count - 1):
            token = reader.varint()
            gap = token if mode == 0 else previous + unzigzag(token)
            offsets.append(offsets[-1] + gap * quantum)
            previous = gap
    return offsets


def transform(records, predictor, base):
    if predictor == 0:
        return canonical(records)
    order = [r.kind - 1 for r in records]
    out = bytearray(bitpack(order, 2))
    for kind in range(1, 5):
        rows = [r for r in records if r.kind == kind]
        if not rows:
            continue
        offsets = [r.us - base for r in rows]
        if predictor == 8:
            out.extend(adaptive_times(offsets))
        elif predictor == 1:
            out.extend(struct.pack('<' + 'I' * len(rows), *offsets))
        else:
            out.extend(varint(offsets[0]))
            previous_delta = 0
            for i in range(1, len(rows)):
                delta = offsets[i] - offsets[i - 1]
                out.extend(varint(delta) if predictor == 2 else varint(zigzag(delta - previous_delta)))
                previous_delta = delta
        if predictor <= 3:
            out.extend(b''.join(struct.pack(FORMATS[kind], *r.values) for r in rows))
        else:
            for index, fmt in enumerate(COLUMNS[kind]):
                column = [r.values[index] for r in rows]
                out.extend(adaptive_column(column, fmt) if predictor == 8 else
                           encode_column(column, fmt, predictor in (5, 7), predictor in (6, 7)))
    if len(out) > MAX_BODY:
        raise ValueError('transform workspace bound')
    return bytes(out)


def inverse(body, predictor, base, counts):
    reader = Reader(body)
    total = sum(counts)
    if predictor == 0:
        records = []
        for _ in range(total):
            kind, schema, size, us = struct.unpack('<BBHQ', reader.read(12))
            if kind not in FORMATS or schema != 1 or size != LENGTHS[kind]:
                raise ValueError('invalid raw record')
            records.append(Record(kind, us, struct.unpack(FORMATS[kind], reader.read(size - 12))))
    else:
        order = bitunpack(reader.read((total * 2 + 7) // 8), total, 2)
        if [order.count(i) for i in range(4)] != list(counts):
            raise ValueError('order/count mismatch')
        streams = {}
        for kind, count in enumerate(counts, 1):
            if not count:
                continue
            if predictor == 8:
                offsets = inverse_times(reader, count)
            elif predictor == 1:
                offsets = [reader.number('I') for _ in range(count)]
            else:
                offsets, previous_delta = [reader.varint()], 0
                for _ in range(count - 1):
                    encoded = reader.varint()
                    delta = encoded if predictor == 2 else previous_delta + unzigzag(encoded)
                    offsets.append(offsets[-1] + delta)
                    previous_delta = delta
            if any(not 0 <= n + base < (1 << 64) - 1 for n in offsets):
                raise ValueError('timestamp overflow')
            if predictor <= 3:
                rows = [struct.unpack(FORMATS[kind], reader.read(LENGTHS[kind] - 12)) for _ in range(count)]
            else:
                columns = [inverse_adaptive(reader, count, fmt) if predictor == 8 else
                           decode_column(reader, count, fmt, predictor in (5, 7), predictor in (6, 7))
                           for fmt in COLUMNS[kind]]
                rows = list(zip(*columns))
            streams[kind] = iter([Record(kind, base + offset, tuple(values)) for offset, values in zip(offsets, rows)])
        records = [next(streams[kind + 1]) for kind in order]
    reader.done()
    if [sum(r.kind == k for r in records) for k in range(1, 5)] != list(counts):
        raise ValueError('record count mismatch')
    if any(a.us > b.us for a, b in zip(records, records[1:])):
        raise ValueError('backwards monotonic timestamps')
    return records


def lzss_encode(data):
    """1024-byte window, <=8 candidates/search, 3..18-byte matches, no dictionary across blocks."""
    output, positions, at = bytearray(), defaultdict(list), 0
    while at < len(data):
        control_at = len(output)
        output.append(0)
        control = 0
        for bit in range(8):
            if at == len(data):
                break
            candidates = positions[data[at:at + 3]]
            best_length, distance = 0, 0
            for previous in reversed(candidates[-8:]):
                if at - previous > 1024:
                    continue
                length = 0
                while length < min(18, len(data) - at) and data[previous + length] == data[at + length]:
                    length += 1
                if length >= 3 and length > best_length:
                    best_length, distance = length, at - previous
            take = best_length if best_length >= 3 else 1
            if best_length >= 3:
                output.extend(struct.pack('<H', ((distance - 1) << 4) | (take - 3)))
            else:
                control |= 1 << bit
                output.append(data[at])
            for p in range(at, at + take):
                key = data[p:p + 3]
                positions[key].append(p)
                if len(positions[key]) > 8:
                    positions[key].pop(0)
            at += take
        output[control_at] = control
    return bytes(output)


def lzss_decode(data, expected):
    reader, output = Reader(data), bytearray()
    if expected > MAX_BODY:
        raise ValueError('output bound')
    while len(output) < expected:
        control = reader.number('B')
        for bit in range(8):
            if len(output) == expected:
                if control >> bit:
                    raise ValueError('nonzero control padding')
                break
            if control & (1 << bit):
                output.extend(reader.read(1))
            else:
                token = reader.number('H')
                if token & 0xC000:
                    raise ValueError('invalid match')
                distance, length = (token >> 4) + 1, (token & 15) + 3
                if distance > len(output) or len(output) + length > expected:
                    raise ValueError('invalid match/output bound')
                for _ in range(length):
                    output.append(output[-distance])
    reader.done()
    return bytes(output)


def encode_block(records, predictor, backend, sequence=0):
    if not records or len(records) > 256:
        raise ValueError('record count bound')
    base = min(r.us for r in records)
    if any(a.us > b.us for a, b in zip(records, records[1:])):
        raise ValueError('input ordering')
    counts = [sum(r.kind == k for r in records) for k in range(1, 5)]
    raw = canonical(records)
    if len(raw) > MAX_BODY:
        raise ValueError('block bound')
    try:
        body = transform(records, predictor, base)
    except (ValueError, struct.error):
        predictor, body = 0, raw
    actual_backend = 0
    if backend == 1:
        candidate = struct.pack('<H', len(body)) + lzss_encode(body)
        if len(candidate) < len(body):
            body, actual_backend = candidate, backend
    elif backend in (2, 3):
        compressor = zlib.compressobj(level=1, wbits=-9 if backend == 2 else -15,
                                      memLevel=1 if backend == 2 else 8)
        candidate = struct.pack('<H', len(body)) + compressor.compress(body) + compressor.flush()
        if len(candidate) < len(body):
            body, actual_backend = candidate, backend
    # Unconditional bounded raw fallback, including transform expansion.
    if len(body) > len(raw):
        predictor, actual_backend, body = 0, 0, raw
    header = HEADER.pack(b'TCB0', 0, predictor, actual_backend, 0, sequence,
                         len(raw), len(body), *counts, base)
    frame = header + body
    return frame + struct.pack('<I', zlib.crc32(frame))


def decode_block(frame):
    if len(frame) < HEADER.size + 4 or len(frame) > HEADER.size + MAX_BODY + 4:
        raise ValueError('frame bound')
    if struct.unpack('<I', frame[-4:])[0] != zlib.crc32(frame[:-4]):
        raise ValueError('block CRC')
    magic, version, predictor, backend, reserved, seq, raw_size, stored, *tail = HEADER.unpack(frame[:HEADER.size])
    counts, base = tail[:4], tail[4]
    if magic != b'TCB0' or version or reserved or predictor >= len(PREDICTORS) or backend > 3:
        raise ValueError('unsupported block')
    if not 0 < sum(counts) <= 256 or raw_size != sum(count * LENGTHS[k] for k, count in enumerate(counts, 1)):
        raise ValueError('canonical length/count bound')
    if raw_size > MAX_BODY or len(frame) != HEADER.size + stored + 4:
        raise ValueError('stored size bound')
    body = frame[HEADER.size:-4]
    if backend:
        reader = Reader(body)
        expected = reader.number('H')
        if expected > MAX_BODY:
            raise ValueError('decompression bound')
        payload = reader.read(len(body) - 2)
        if backend == 1:
            body = lzss_decode(payload, expected)
        else:
            inflater = zlib.decompressobj(wbits=-9 if backend == 2 else -15)
            try:
                body = inflater.decompress(payload, expected + 1)
            except zlib.error as exc:
                raise ValueError('invalid deflate stream') from exc
            if len(body) != expected or not inflater.eof or inflater.unused_data or inflater.unconsumed_tail:
                raise ValueError('deflate length/stream bound')
    records = inverse(body, predictor, base, counts)
    if len(canonical(records)) != raw_size:
        raise ValueError('decoded length')
    if base != min(r.us for r in records):
        raise ValueError('block timestamp base')
    return seq, records


def page_image(frames, pages_per_sector=14):
    """Exact occupied bytes. TCB0 frame boundaries start at a new flash page.

    Existing 16-byte page envelope/CRC/marker positions retained for this model;
    chunk index and count occupy the existing count word. Blocks may span sectors.
    """
    if not 1 <= pages_per_sector <= 15:
        raise ValueError('geometry')
    pages = []
    for block, frame in enumerate(frames):
        chunks = math.ceil(len(frame) / PAYLOAD)
        for chunk in range(chunks):
            content = frame[chunk * PAYLOAD:(chunk + 1) * PAYLOAD]
            p = bytearray(b'\xff' * 256)
            p[:16] = struct.pack('<4sIIBBH', b'TPC0', 1, len(pages), chunk, chunks, len(content))
            p[16:16 + len(content)] = content
            struct.pack_into('<I', p, 248, zlib.crc32(p[:248]))
            struct.pack_into('<I', p, 252, 0xC0DEC0DE)
            pages.append(bytes(p))
    sectors = math.ceil(len(pages) / pages_per_sector)
    out = bytearray(b'\xff' * (sectors * 4096))
    for i, p in enumerate(pages):
        sector, index = divmod(i, pages_per_sector)
        out[sector * 4096 + (index + 1) * 256:sector * 4096 + (index + 2) * 256] = p
    # Synthetic committed sector headers/checkpoints; not V2 data or firmware IO.
    for sector in range(sectors):
        h = bytearray(b'\xff' * 256)
        h[:16] = struct.pack('<4sIII', b'TPH0', 1, sector * pages_per_sector, sector)
        struct.pack_into('<I', h, 248, zlib.crc32(h[:248]))
        struct.pack_into('<I', h, 252, 0xC0DEC0DE)
        out[sector * 4096:sector * 4096 + 256] = h
    return bytes(out), len(pages)


def recover(image, pages_per_sector=14):
    frames, issues, pending = [], [], []
    next_sequence = 0
    for sector in range(len(image) // 4096):
        h = image[sector * 4096:sector * 4096 + 256]
        valid = lambda p: len(p) == 256 and struct.unpack('<I', p[252:])[0] == 0xC0DEC0DE and struct.unpack('<I', p[248:252])[0] == zlib.crc32(p[:248])
        if not valid(h) or h[:16] != struct.pack('<4sIII', b'TPH0', 1, sector * pages_per_sector, sector):
            issues.append('header'); pending = []; next_sequence = (sector + 1) * pages_per_sector
            continue
        for index in range(1, pages_per_sector + 1):
            p = image[sector * 4096 + index * 256:sector * 4096 + (index + 1) * 256]
            if p == b'\xff' * 256:
                break
            if not valid(p):
                issues.append('page'); pending = []; next_sequence = (sector + 1) * pages_per_sector
                break
            magic, session, sequence, chunk, chunks, used = struct.unpack('<4sIIBBH', p[:16])
            if magic != b'TPC0' or session != 1 or sequence != next_sequence or not 1 <= chunks <= 36 or chunk >= chunks or not 0 < used <= 232 or p[16 + used:248] != b'\xff' * (232 - used):
                issues.append('envelope'); pending = []; next_sequence = sequence + 1
                continue
            next_sequence += 1
            if chunk == 0:
                pending = []
            if chunk != len(pending):
                issues.append('chunk'); pending = []
                continue
            pending.append((chunks, p[16:16 + used]))
            if any(n != chunks for n, _ in pending):
                issues.append('chunk count'); pending = []
                continue
            if len(pending) == chunks:
                try:
                    _, records = decode_block(b''.join(part for _, part in pending))
                    frames.append(records)
                except ValueError:
                    issues.append('block')
                pending = []
    if pending:
        issues.append('incomplete block')
    return frames, issues


def stream_image(frames, pages_per_sector=14):
    """Experimental stream packing; page header records first block start for resync."""
    starts, length = [], 0
    for frame in frames:
        starts.append(length)
        length += len(frame)
    stream = b''.join(frames)
    pages = math.ceil(len(stream) / PAYLOAD)
    output = bytearray(b'\xff' * (math.ceil(pages / pages_per_sector) * 4096))
    for sector in range(len(output) // 4096):
        header = bytearray(b'\xff' * 256)
        header[:16] = struct.pack('<4sIII', b'TPH0', 1, sector * pages_per_sector, sector)
        struct.pack_into('<I', header, 248, zlib.crc32(header[:248]))
        struct.pack_into('<I', header, 252, 0xC0DEC0DE)
        output[sector * 4096:sector * 4096 + 256] = header
    first = 0
    for page in range(pages):
        at = page * PAYLOAD
        content = stream[at:at + PAYLOAD]
        while first < len(starts) and starts[first] < at:
            first += 1
        offset = starts[first] - at if first < len(starts) and starts[first] < at + PAYLOAD else 0xFFFF
        data = bytearray(b'\xff' * 256)
        data[:16] = struct.pack('<4sIIHH', b'TPS0', 1, page, offset, len(content))
        data[16:16 + len(content)] = content
        struct.pack_into('<I', data, 248, zlib.crc32(data[:248]))
        struct.pack_into('<I', data, 252, 0xC0DEC0DE)
        sector, index = divmod(page, pages_per_sector)
        start = sector * 4096 + (index + 1) * 256
        output[start:start + 256] = data
    return bytes(output), pages


def recover_stream(image, pages_per_sector=14):
    recovered, issues, pending = [], [], bytearray()
    last_block = -1
    for sector in range(len(image) // 4096):
        valid = lambda p: len(p) == 256 and struct.unpack('<I', p[252:])[0] == 0xC0DEC0DE and struct.unpack('<I', p[248:252])[0] == zlib.crc32(p[:248])
        header = image[sector * 4096:sector * 4096 + 256]
        if not valid(header) or header[:16] != struct.pack('<4sIII', b'TPH0', 1, sector * pages_per_sector, sector):
            issues.append('header'); pending.clear(); continue
        for index in range(pages_per_sector):
            at = sector * 4096 + (index + 1) * 256
            page = image[at:at + 256]
            if page == b'\xff' * 256:
                break
            if not valid(page):
                issues.append('page'); pending.clear(); break
            magic, session, sequence, first, used = struct.unpack('<4sIIHH', page[:16])
            if magic != b'TPS0' or session != 1 or sequence != sector * pages_per_sector + index or not 0 < used <= PAYLOAD or (first != 0xFFFF and first >= used) or page[16 + used:248] != b'\xff' * (232 - used):
                issues.append('page envelope'); pending.clear(); break
            content = page[16:16 + used]
            if not pending:
                if first == 0xFFFF:
                    continue
                pending.extend(content[first:])
            else:
                pending.extend(content)
            while len(pending) >= HEADER.size:
                stored = struct.unpack_from('<H', pending, 14)[0]
                size = HEADER.size + stored + 4
                if pending[:4] != b'TCB0' or stored > MAX_BODY:
                    issues.append('block header'); pending.clear(); break
                if len(pending) < size:
                    break
                frame = bytes(pending[:size])
                del pending[:size]
                try:
                    sequence, records = decode_block(frame)
                    if sequence <= last_block:
                        raise ValueError('block sequence')
                    recovered.append(records)
                    last_block = sequence
                except ValueError:
                    issues.append('block')
            if len(pending) > HEADER.size + MAX_BODY + 4:
                raise ValueError('recovery buffer bound')
    if pending:
        issues.append('incomplete block')
    return recovered, issues


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def load_real(path):
    text = path.read_text(encoding='utf-8')
    if 'BEGIN_RECORD_CSV' not in text:
        return [], {'source': str(path), 'reason': 'no V1 CSV'}
    csv_text = text.split('BEGIN_RECORD_CSV', 1)[1].split('END_RECORD_CSV', 1)[0]
    rows = list(csv.DictReader(io.StringIO('\n'.join(line for line in csv_text.splitlines() if line.strip()))))
    samples = []
    fields = ['accel_x_g', 'accel_y_g', 'accel_z_g', 'gyro_x_dps', 'gyro_y_dps', 'gyro_z_dps']
    for row in rows:
        axes = []
        for name, scale in zip(fields, [0.000061] * 3 + [0.00875] * 3):
            value = float(row[name])
            raw = round(value / scale)
            # Match the actual firmware float32 conversion/6-decimal printing.
            candidates = [n for n in (raw - 1, raw, raw + 1) if -32768 <= n <= 32767 and
                          abs(value - f32(n * f32(scale))) <= 0.00000051]
            if len(candidates) != 1:
                raise ValueError('CSV cannot unambiguously reconstruct raw precision')
            axes.append(candidates[0])
        samples.append(Record(1, int(row['timestamp_ms']) * 1000, tuple(axes)))
    if len({r['session'] for r in rows}) != 1:
        raise ValueError('real source must be split into sessions')
    metadata = {'source': str(path.relative_to(ROOT)), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                'samples': len(samples), 'span_seconds': (samples[-1].us - samples[0].us) / 1e6,
                'raw_counts_reconstructed': True, 'csv_float32_roundtrip_verified': True,
                'timestamp_precision': 'original V1 milliseconds, converted exactly to microseconds',
                'gps': 'synthetic; no GPS is present in the source CSV', 'motion_label': 'unlabelled real recording'}
    return samples, metadata


def synthetic(profile, seconds, seed):
    rng = random.Random(seed)
    samples = []
    for i in range(seconds * 100):
        t = i / 100
        if profile == 'adversarial_uniform':
            axes = [rng.randrange(-32768, 32768) for _ in range(6)]
        else:
            sigma = {'stillness': (4, 12), 'walking': (20, 20), 'sprinting': (80, 60),
                     'jumping': (30, 30), 'rapid_turns': (40, 40), 'noisy': (500, 700)}[profile]
            base = [250, -400, 16200, 180, -300, 90]
            frequency = {'stillness': 0.08, 'walking': 1.8, 'sprinting': 3.2,
                         'jumping': 0.65, 'rapid_turns': 1.0, 'noisy': 2.0}[profile]
            aa, ga = {'stillness': (3, 2), 'walking': (2500, 2000), 'sprinting': (8000, 7000),
                      'jumping': (2000, 2500), 'rapid_turns': (4000, 14000), 'noisy': (6000, 6000)}[profile]
            axes = [base[a] + (aa if a < 3 else ga) * math.sin(2 * math.pi * frequency * t + a * 0.7) +
                    (0 if profile == 'stillness' else (aa if a < 3 else ga) * 0.18 * math.sin(2 * math.pi * frequency * 3 * t + a)) +
                    rng.gauss(0, sigma[a >= 3]) for a in range(6)]
            if profile == 'jumping':
                phase = t % 1.5
                axes[2] += 23000 * math.exp(-((phase - 0.2) / 0.035) ** 2) - 12000 * math.exp(-((phase - 0.7) / 0.17) ** 2)
            if profile == 'rapid_turns':
                axes[5] += 13000 * math.tanh(5 * math.sin(t * math.pi))
            axes = [max(-32768, min(32767, round(n))) for n in axes]
        jitter = rng.randint(-200, 200)
        if i and i % 137 == 0:
            jitter += 2300
        samples.append(Record(1, 1000000 + i * 10000 + jitter, tuple(axes)))
    return samples


def gps_rows(samples, profile, seed, start_index=0):
    records = list(samples)
    for i in range(0, len(samples), 10):
        absolute_index = start_index + i
        # Per-epoch randomness makes 1s and 2s runs use IDENTICAL input bytes.
        rng = random.Random(seed + absolute_index * 104729)
        t = absolute_index / 100
        moving = profile != 'stillness'
        lat = 507752560 + round((t * 9 if moving else 0) + rng.gauss(0, 3))
        lon = -900030 + round((t * 5 if moving else 0) + rng.gauss(0, 3))
        speed = max(0, round((3000 + 200 * math.sin(t) if moving else 30) + rng.gauss(0, 15)))
        course = round((84 + (20 * math.sin(t / 2) if profile == 'rapid_turns' else 2 * math.sin(t))) * 100000)
        # Include loss of fix and missing fields instead of assuming perpetual A.
        invalid = 37 <= absolute_index % 5000 // 100 < 40
        mask, status = (125, ord('V')) if invalid else (127, ord('A'))
        if absolute_index % 3130 == 0 and absolute_index:
            mask &= ~32
        values = (mask, lat, lon, speed, course, 20261030,
                  (58460000 + absolute_index * 10) % 86400000 if mask & 32 else 0, status, ord('A'), 0x4E47)
        records.append(Record(2, samples[i].us + 20000 + rng.randint(-1000, 1000), values))
        if i % 100 == 0:
            values = (63, 20261030, (58460000 + absolute_index * 10) % 86400000,
                      round(1200 + rng.gauss(0, 200)), 45000, rng.choice((110, 120, 128, 135)), rng.choice((7, 8, 9)),
                      0 if invalid else 1, 0x4E47, 0)
            records.append(Record(3, samples[i].us + 40000 + rng.randint(-1000, 1000), values))
    return sorted(records, key=lambda r: r.us)


def datasets(seconds, block_seconds=1):
    block_samples = block_seconds * 100
    profiles = ('stillness', 'walking', 'sprinting', 'jumping', 'rapid_turns', 'noisy', 'adversarial_uniform')
    for index, profile in enumerate(profiles):
        samples = synthetic(profile, seconds, 17300 + index)
        yield profile, [gps_rows(samples[i:i + block_samples], profile, 5000 + index * 100000, i) for i in range(0, len(samples), block_samples)], {
            'kind': 'synthetic', 'seed': 17300 + index, 'seconds': seconds,
            'imu_timestamp_jitter': '+/-200us, plus 2300us delay every 137 samples',
            'gps': 'synthetic 10Hz RMC/1Hz GGA with timestamp/position noise and invalid/missing fields'}
    real, meta = load_real(ROOT / 'logs/device-monitor-261027-204904.log')
    if real:
        meta.update(kind='real IMU + synthetic GPS', full_blocks=len(real) // block_samples, tail_samples=len(real) % block_samples,
                    capacity_normalisation='100 IMU observations per second, not slower real arrival rate')
        yield 'real_recording', [gps_rows(real[i:i + block_samples], 'real_recording', 9000, i)
                                 for i in range(0, len(real) // block_samples * block_samples, block_samples)], meta


def allocation(page_counts, seconds, pages_per_sector=14, block_seconds=1):
    # Repeat measured block-size sequence to the target, then round to whole sectors.
    full, tail = divmod(math.ceil(seconds / block_seconds), len(page_counts))
    pages = sum(page_counts) * full + sum(page_counts[:tail])
    recording = math.ceil(pages / pages_per_sector) * 4096
    return {'data_pages': pages, 'recording_bytes': recording, 'chip_bytes_with_diagnostic': recording + 4096,
            'fits': recording <= CAPACITY}


def duration(page_counts, pages_per_sector=14, block_seconds=1):
    capacity_pages = (CAPACITY // 4096) * pages_per_sector
    cycles, spare = divmod(capacity_pages, sum(page_counts))
    blocks = cycles * len(page_counts)
    for pages in page_counts:
        if pages > spare:
            break
        spare -= pages
        blocks += 1
    return blocks * block_seconds / 60


def stream_allocation(sizes, seconds, pages_per_sector=14, block_seconds=1):
    cycles, tail = divmod(math.ceil(seconds / block_seconds), len(sizes))
    body_bytes = cycles * sum(sizes) + sum(sizes[:tail])
    # Fourteen-page geometry already reserves a whole footer page per sector.
    # Fifteen-page geometry instead reserves up to 256 payload bytes for END.
    pages = math.ceil((body_bytes + (256 if pages_per_sector == 15 else 0)) / PAYLOAD)
    recording = math.ceil(pages / pages_per_sector) * 4096
    return {'data_pages': pages, 'recording_bytes': recording,
            'chip_bytes_with_diagnostic': recording + 4096, 'fits': recording <= CAPACITY}


def stream_duration(sizes, pages_per_sector=14, block_seconds=1):
    capacity = (CAPACITY // 4096) * pages_per_sector * PAYLOAD - (256 if pages_per_sector == 15 else 0)
    cycles, spare = divmod(capacity, sum(sizes))
    count = cycles * len(sizes)
    for size in sizes:
        if size > spare:
            break
        spare -= size
        count += 1
    return count * block_seconds / 60


def benchmark(seconds, out, block_seconds=1):
    report = {'contract': 'experimental TCB0; frozen V2 and active firmware untouched',
              'seconds_per_synthetic_case': seconds, 'block_seconds': block_seconds,
              'capacity_bytes': 16777216, 'recording_bytes': CAPACITY,
              'data_payload_bytes_per_page': 232, 'default_data_pages_per_sector': 14,
              'host_python': platform.python_version(), 'host_platform': platform.platform(),
              'zlib_version': zlib.ZLIB_RUNTIME_VERSION,
              'cases': {}, 'host_timing_warning': 'CPython performance only, not MCU execution time'}
    for name, blocks, source in datasets(seconds, block_seconds):
        print('Benchmarking', name, len(blocks), 'blocks', flush=True)
        results = {}
        frames_by_codec = {}
        for pi, predictor in enumerate(PREDICTORS):
            for bi, backend in enumerate(BACKENDS):
                key = predictor + '+' + backend
                frames, encode_times, decode_times, fallback = [], [], [], 0
                for index, records in enumerate(blocks):
                    start = time.perf_counter_ns()
                    frame = encode_block(records, pi, bi, index)
                    encode_times.append(time.perf_counter_ns() - start)
                    start = time.perf_counter_ns()
                    sequence, decoded = decode_block(frame)
                    decode_times.append(time.perf_counter_ns() - start)
                    if sequence != index or canonical(decoded) != canonical(records):
                        raise AssertionError('lossless reconstruction failed')
                    fallback += int(frame[5] != pi or frame[6] != bi)
                    frames.append(frame)
                pages = [math.ceil(len(f) / PAYLOAD) for f in frames]
                results[key] = {'mean_block_bytes': statistics.mean(map(len, frames)), 'max_block_bytes': max(map(len, frames)),
                                'mean_pages': statistics.mean(pages), 'max_pages': max(pages),
                                'duration_minutes': duration(pages, block_seconds=block_seconds),
                                'duration_with_15_data_pages': duration(pages, 15, block_seconds),
                                'encode_host_us_mean': statistics.mean(encode_times) / 1000,
                                'encode_host_us_max': max(encode_times) / 1000,
                                'decode_host_us_mean': statistics.mean(decode_times) / 1000,
                                'decode_host_us_max': max(decode_times) / 1000,
                                'fallback_blocks': fallback, 'blocks': len(frames), 'roundtrip': 'byte-exact all records',
                                'targets': {str(m): allocation(pages, m * 60, block_seconds=block_seconds) for m in (180, 240, 300)}}
                frames_by_codec[key] = frames
        # Adaptive selection uses only bounded custom LZSS and bit/varint coding.
        lightweight = [k for k in results if 'deflate' not in k]
        selected = [min((frames_by_codec[k][i] for k in lightweight), key=len) for i in range(len(blocks))]
        pages = [math.ceil(len(f) / PAYLOAD) for f in selected]
        raw_total = sum(len(canonical(b)) for b in blocks)
        adaptive = {'mean_block_bytes': statistics.mean(map(len, selected)), 'max_block_bytes': max(map(len, selected)),
                    'mean_pages': statistics.mean(pages), 'max_pages': max(pages),
                    'duration_minutes': duration(pages, block_seconds=block_seconds),
                    'duration_with_15_data_pages': duration(pages, 15, block_seconds),
                    'selected_modes': dict((k, sum(f[5] == PREDICTORS.index(k.split('+')[0]) and
                                                        f[6] == BACKENDS.index(k.split('+')[1]) for f in selected)) for k in lightweight),
                    'canonical_to_block_ratio': raw_total / sum(map(len, selected)),
                    'targets': {str(m): allocation(pages, m * 60, block_seconds=block_seconds) for m in (180, 240, 300)}}
        image, count = page_image(selected)
        recovered, issues = recover(image)
        if issues or [canonical(r) for r in recovered] != [canonical(r) for r in blocks]:
            raise AssertionError('page recovery failed')
        for geometry in (14, 15):
            stream, stream_pages = stream_image(selected, geometry)
            recovered, issues = recover_stream(stream, geometry)
            if issues or [canonical(r) for r in recovered] != [canonical(r) for r in blocks]:
                raise AssertionError('stream page recovery failed')
        sizes = list(map(len, selected))
        adaptive['stream_packed_minutes'] = stream_duration(sizes, 14, block_seconds)
        adaptive['stream_packed_15page_minutes'] = stream_duration(sizes, 15, block_seconds)
        adaptive['stream_targets_14pages'] = {str(m): stream_allocation(sizes, m * 60, 14, block_seconds) for m in (180, 240, 300)}
        adaptive['stream_targets_15pages'] = {str(m): stream_allocation(sizes, m * 60, 15, block_seconds) for m in (180, 240, 300)}
        report['cases'][name] = {'source': source, 'blocks': len(blocks), 'records_per_full_block': len(blocks[0]),
                                'results': results, 'adaptive_lightweight': adaptive,
                                'selected_image_bytes': len(image), 'selected_image_data_pages': count,
                                'canonical_sha256': hashlib.sha256(b''.join(canonical(b) for b in blocks)).hexdigest()}
    out.mkdir(parents=True, exist_ok=True)
    (out / 'results.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    lines = ['# Host compression benchmark results', '', 'Generated host results; synthetic capacities are projections, not hardware guarantees.', '',
             '| Case | Lightweight bytes/block | Pages/block | Minutes, 14 data pages | Minutes, 15 data pages |',
             '|---|---:|---:|---:|---:|']
    for name, data in report['cases'].items():
        r = data['adaptive_lightweight']
        lines.append(f"| {name} | {r['mean_block_bytes']:.1f} | {r['mean_pages']:.3f} | {r['duration_minutes']:.1f} | {r['duration_with_15_data_pages']:.1f} |")
    lines += ['', 'See results.json for every predictor/backend, exact target allocations, maxima, host timing and provenance.', '']
    (out / 'summary.md').write_text('\n'.join(lines), encoding='utf-8')
    return report


def overview(out):
    reports = {n: json.loads((out / n / 'results.json').read_text(encoding='utf-8'))
               for n in ('one-second', 'two-second')}
    lines = ['# Existing 16 MiB flash: lossless storage experiment', '',
             'Experimental host benchmark only. No active firmware or frozen V2 specification changes.', '',
             'Seven deterministic 120-second synthetic cases plus 3,000 real CSV-derived IMU samples.',
             'The complete 3,026-sample real source, including its partial tail, is round-trip tested.',
             'GPS is synthetic in every row. Capacity is normalised to 100Hz IMU, not the real log\'s slower arrival rate.', '',
             '| Case | 1s aligned, 14 pages | 2s aligned, 14 pages | 2s stream, 14 pages | 2s stream, 15 pages |',
             '|---|---:|---:|---:|---:|']
    for name in reports['one-second']['cases']:
        one = reports['one-second']['cases'][name]['adaptive_lightweight']
        two = reports['two-second']['cases'][name]['adaptive_lightweight']
        lines.append(f"| {name} | {one['duration_minutes']:.1f} min | {two['duration_minutes']:.1f} min | "
                     f"{two['stream_packed_minutes']:.1f} min | {two['stream_packed_15page_minutes']:.1f} min |")
    lines += ['', 'Durations repeat the measured block-size sequence to flash capacity. They are not endurance tests or guarantees.',
              'All block headers, block CRCs, page CRC/markers, page padding, sector headers, final-sector rounding and the diagnostic sector are counted.',
              '15-page stream projections additionally reserve 256 payload bytes for a future clean-end frame; its command/format is not implemented.',
              '15-page sectors require a new format with an appended session-end frame instead of the existing per-sector reserved footer.', '',
              '## Real recording: codec comparison, two-second blocks', '',
              '| Codec | Encoded bytes/block | Aligned pages/block | Aligned capacity | Mean host encode time |',
              '|---|---:|---:|---:|---:|']
    results = reports['two-second']['cases']['real_recording']['results']
    for key in ('v2_raw+none', 'block_u32+none', 'time_delta+none', 'time_dod+none', 'delta_varint+none',
                'dod_varint+none', 'delta_bitpack+none', 'dod_bitpack+none', 'adaptive_rice+none',
                'adaptive_rice+lzss1k', 'adaptive_rice+deflate512', 'adaptive_rice+deflate32k'):
        r = results[key]
        lines.append(f"| {key} | {r['mean_block_bytes']:.1f} | {r['mean_pages']:.3f} | {r['duration_minutes']:.1f} min | {r['encode_host_us_mean']/1000:.3f} ms |")
    lines += ['', 'CPython timings are not nRF52840 timings; candidates are explored offline. Full DEFLATE32k is a comparison, not a recommended embedded encoder.',
              'The adaptive lightweight oracle excludes both DEFLATE backends and stores its selected codec ID in each block.', '',
              'See [design and interpretation](../../docs/storage-compression-review.md) and each results.json for source hashes, maxima, target allocations and per-codec evidence.', '']
    (out / 'summary.md').write_text('\n'.join(lines), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=120)
    parser.add_argument('--block-seconds', type=int, choices=(1, 2), default=1)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--summarise', action='store_true', help='read both existing benchmark runs and write an overview')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error('seconds must be 1..3600')
    if args.seconds % args.block_seconds:
        parser.error('seconds must be divisible by block-seconds')
    if args.summarise:
        overview(args.output or ROOT / 'benchmarks/storage')
    else:
        benchmark(args.seconds, args.output or ROOT / 'benchmarks/storage' / ('one-second' if args.block_seconds == 1 else 'two-second'), args.block_seconds)
