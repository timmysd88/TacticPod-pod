"""Post-test evidence capture; retain and report incomplete/loss sessions too. No start, stop, erase, calibration or reset."""
from pathlib import Path
import sys, time, json, re, hashlib
import serial
from serial.tools import list_ports

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tools'))
from tcb3_download import capture
from tcb3_decode import decode_packets, decode_image, json_document

result = {'passed': False, 'uploaded': False, 'recording_started': False}
ports = [p for p in list_ports.comports() if p.vid == 0x2886
         and p.pid == 0x8044 and p.serial_number == '0E8A89B7289430BD']
assert len(ports) == 1, 'Hardware identity uncertain; stop'
port = ports[0]
result['device'] = dict(port=port.device, vid=port.vid, pid=port.pid,
                        serial=port.serial_number, description=port.description)
allowed = {'RECORD STATUS', 'OBS STATUS', 'COMP STATUS', 'TCB STATUS',
           'BAT STATUS', 'GPS STATUS', 'DIAG ID'}
try:
    with (OUT / 'after-test-transcript.txt').open('xb') as log:
        with serial.Serial(port.device, 115200, timeout=.05, write_timeout=1) as link:
            def read(seconds, marker=None):
                end = time.monotonic() + seconds
                data = bytearray()
                while time.monotonic() < end:
                    b = link.read(max(1, link.in_waiting))
                    if b:
                        data.extend(b); log.write(b); log.flush()
                    if marker and marker.encode() in data:
                        tail = data[data.index(marker.encode()):]
                        if b'\n' in tail: break
                text = data.decode('utf-8', 'replace')
                assert not marker or marker in text, ('Response timeout', marker, text)
                return text
            def command(cmd, marker, seconds=8):
                assert cmd in allowed
                log.write(('\nHOST TX ' + cmd + '\n').encode()); log.flush()
                link.write((cmd + '\n').encode()); link.flush()
                return read(seconds, marker)
            result['startup'] = read(12)
            states = {}
            for cmd, marker in [('RECORD STATUS','RECORD STATUS:'),
                                ('OBS STATUS','OBS END'),('COMP STATUS','COMP END'),
                                ('TCB STATUS','TCB STATUS')]:
                states[cmd] = command(cmd, marker)
            result['states'] = states
            assert 'RECORD STATUS: STOPPED' in states['RECORD STATUS'], states
            assert 'capture=OFF' in states['OBS STATUS'] and 'RAM_fast_imu=OFF' in states['OBS STATUS'], states
            assert 'capturing=0 fault=0' in states['COMP STATUS'] and 'input=0/2 output=0/2' in states['COMP STATUS'], states
            assert 'state=1 ' in states['TCB STATUS'] and 'error=none' in states['TCB STATUS'] and 'lock=0' in states['TCB STATUS'], states
            result['battery'] = command('BAT STATUS', 'BAT source=')
            result['gps'] = command('GPS STATUS', 'GPS speed_')
            identity = command('DIAG ID', 'END_FLASH_ID', 15)
            result['flash_identity'] = identity
            assert re.findall(r'FLASH ID JEDEC \d+ ([0-9A-F]+)', identity) == ['0B4018'] * 5
            assert 'SR1 before=00 after=00 SR2=02' in identity
            assert 'JEDEC_repeat=PASS SFDP_repeat=PASS' in identity
            parts = re.findall(r'FLASH ID SFDP pass=(\d+) address=([0-9A-F]+) ([0-9A-F]+)', identity)
            assert len(parts) == 32
            reference = (ROOT/'logs/phase3b4-hardware/sfdp-capture-1.bin').read_bytes()
            for _, address, value in parts:
                address = int(address, 16)
                assert bytes.fromhex(value) == reference[address:address+16]
            read(.3)
            with (OUT/'after-test.tpx3').open('xb') as target:
                result['archive'] = capture(link, target, timeout=120)
            log.write(b'\nHOST RX archive saved; serial closing\n')
    data = (OUT/'after-test.tpx3').read_bytes()
    image, packet_issues = decode_packets(data)
    decoded = decode_image(image)
    decoded['export_issues'] = packet_issues
    with (OUT/'after-test-image.bin').open('xb') as target: target.write(image)
    for session in decoded['sessions']: session.pop('raw', None)
    (OUT/'after-test-decoded.json').write_text(json.dumps(json_document(decoded), indent=2)+'\n')
    result['export_issues']=packet_issues;result['container_issues']=decoded['issues']
    assert decoded['sessions'], 'No sessions recovered'
    result['sessions'] = [dict(id=s['session_id'], status=s['status'], records=len(s['records']),
                              footer=s['footer']) for s in decoded['sessions']]
    result['image_sha256'] = hashlib.sha256(image).hexdigest()
    result['capture_decoded'] = True
    result['passed'] = not packet_issues and not decoded['issues'] and all(s['status']=='complete' and not s['issues'] for s in decoded['sessions'])
    print(json.dumps(result['sessions'], indent=2), flush=True)
finally:
    result['serial_released'] = True
    (OUT/'after-test-capture-result.json').write_text(json.dumps(result, indent=2)+'\n')
