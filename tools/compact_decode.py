"""Strict host decoder for RAM-only compact block format 3.0 (TCB3).

No physical flash extraction, serial IO or changes to the frozen V1/V2 readers.
The integer transforms deliberately share the independently tested benchmark
inverse; V2 semantic validation is applied to every reconstructed wire record.
"""
from pathlib import Path
import argparse
import json
import struct
import sys
import zlib

try:
    from . import storage_benchmark as codec
    from .tacticpod_decode import decode_record, json_document
except ImportError:
    import storage_benchmark as codec
    from tacticpod_decode import decode_record, json_document


def decode_frame(frame):
    if not 36 <= len(frame) <= 8228:
        raise ValueError('compact frame length bound')
    if struct.unpack_from('<I', frame, len(frame)-4)[0] != zlib.crc32(frame[:-4]):
        raise ValueError('compact CRC mismatch')
    magic, minor, predictor, backend, flags, sequence, raw, stored, *tail = codec.HEADER.unpack_from(frame)
    counts, base = tail[:4], tail[4]
    if magic != b'TCB3' or minor or predictor not in (0, 8) or backend or flags:
        raise ValueError('unsupported compact block format')
    if not 0 < sum(counts) <= 256 or raw > 8192 or stored > 8192 or len(frame) != stored+36:
        raise ValueError('compact count/size bound')
    if raw != sum(count*codec.LENGTHS[k] for k, count in enumerate(counts, 1)):
        raise ValueError('compact decoded size mismatch')
    try:
        records = codec.inverse(frame[32:-4], predictor, base, counts)
        wire = codec.canonical(records)
    except (struct.error, OverflowError) as exc:
        raise ValueError('compact reconstructed integer outside wire range') from exc
    if min(r.us for r in records) != base or max(r.us for r in records)-base >= 2000000:
        raise ValueError('compact two-second timestamp window')
    if len(wire) != raw:
        raise ValueError('compact reconstructed size')
    decoded = [decode_record(r.wire()) for r in records]
    return dict(sequence=sequence, predictor=predictor, raw=wire, records=decoded,
                source_bytes=raw, frame_bytes=len(frame))


def decode_stream(data):
    """Frames only. Truncation/corruption fails explicitly, not silent salvage."""
    frames, at, previous = [], 0, -1
    while at < len(data):
        if len(data)-at < 32:
            raise ValueError('interrupted compact header')
        size = struct.unpack_from('<H', data, at+14)[0]+36
        if size > 8228 or at+size > len(data):
            raise ValueError('interrupted compact body')
        frame = decode_frame(data[at:at+size])
        if frame['sequence'] <= previous:
            raise ValueError('compact sequence order')
        previous = frame['sequence'];frames.append(frame);at += size
    return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--raw-output', type=Path, help='new file containing exact reconstructed V2 wire records')
    parser.add_argument('--container', action='store_true', help='decode persistent TPH3 image or TPX3 export')
    args = parser.parse_args()
    try:
        data=args.input.read_bytes()
        if args.container:
            try:from . import tcb3_decode as container
            except ImportError:import tcb3_decode as container
            packet_issues=[]
            if data[:4]==b'TPX3':data,packet_issues=container.decode_packets(data)
            result=container.decode_image(data);result['export_issues']=packet_issues
            if args.raw_output:
                with args.raw_output.open('xb') as stream:stream.write(b''.join(s['raw'] for s in result['sessions']))
            for session in result['sessions']:del session['raw']
            print(json.dumps(container.json_document(result),indent=2,sort_keys=True));return 0
        frames = decode_stream(data)
        if args.raw_output:
            with args.raw_output.open('xb') as stream:
                stream.write(b''.join(f['raw'] for f in frames))
        else:
            for frame in frames:
                del frame['raw']
            print(json.dumps(json_document({'compact_format': '3.0', 'frames': frames}), indent=2, sort_keys=True))
    except (ValueError, OSError) as exc:
        print(str(exc), file=sys.stderr);return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
