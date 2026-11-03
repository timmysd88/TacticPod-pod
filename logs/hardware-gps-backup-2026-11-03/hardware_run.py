"""Approved supervised hardware run. No recording/erase commands or firmware edits."""
from pathlib import Path
import hashlib
import io
import json
import re
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[2]
RUN = sys.argv[2] if len(sys.argv) > 2 else 'hardware-run-20261103'
assert re.fullmatch(r'hardware-run-[a-zA-Z0-9-]+', RUN)
OUT = Path(__file__).with_name(RUN)
NUMBER = '0E8A89B7289430BD'
ENV = 'tacticpod_reusable_usb_gpsbackup'
BASE = '29f90966d868f68b85d63d64d27bab9584f7628a'
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / '.pio-core/test-python')]
import serial
from serial.tools import list_ports
from tcb3_download import capture
from tcb3_decode import decode_packets, decode_image, json_document


def save(name, data):
    (OUT / name).write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')


def identify(pid=0x8044, seconds=2):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        hits = [p for p in list_ports.comports()
                if p.vid == 0x2886 and p.pid == pid and p.serial_number == NUMBER]
        if len(hits) == 1:
            return hits[0]
        time.sleep(.1)
    raise RuntimeError('Expected exactly one matching XIAO; no retry or reset: ' + hex(pid))


def validate_candidate():
    manifest = json.loads((ROOT / 'logs/hardware-gps-backup-2026-11-03/recovery-gate-fix/manifest.json').read_text())
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == BASE
    assert manifest['base_commit'] == BASE and manifest['selected_environment'] == ENV
    assert manifest['software_ready_for_supervised_test'] and manifest['full_suite_exit_code'] == 0
    assert len(manifest['regression_results']) == 32 and not any(manifest['regression_results'].values())
    assert not manifest['automatic_backup_enabled_in_selected_environment']
    for f in manifest['changed_source_and_tests']:
        assert hashlib.sha256((ROOT / f['path']).read_bytes()).hexdigest() == f['sha256'], f['path']
    # Verify all remaining firmware/variant/config files against the verified PR.
    archive = zipfile.ZipFile(io.BytesIO(subprocess.check_output(
        ['git', '-c', 'core.autocrlf=false', 'archive', '--format=zip', BASE], cwd=ROOT)))
    expected_changes = {'src/gps_receiver.cpp', 'src/gps_power.h'}
    for n in archive.namelist():
        if n.endswith('/') or n in expected_changes:
            continue
        if n.startswith(('src/', 'variants/')) or n == 'platformio.ini':
            assert (ROOT/n).read_bytes().replace(b'\r\n', b'\n') == archive.read(n).replace(b'\r\n', b'\n'), n
    patch = ROOT / 'logs/hardware-gps-backup-2026-11-03/recovery-gate-fix/recovery-gate.patch'
    assert hashlib.sha256(patch.read_bytes()).hexdigest() == manifest['patch_sha256']
    artifacts = [a for a in manifest['artifacts'] if a['environment'] == ENV]
    for a in artifacts:
        assert hashlib.sha256((ROOT / a['path']).read_bytes()).hexdigest() == a['sha256'], a['path']
    package = ROOT / next(a['path'] for a in artifacts if a['path'].endswith('firmware.zip'))
    expected_bin = next(a['sha256'] for a in artifacts if a['path'].endswith('firmware.bin'))
    with zipfile.ZipFile(package) as z:
        dfu = json.loads(z.read('manifest.json'))['manifest']
        assert set(dfu) == {'application', 'dfu_version'}
        app = dfu['application']
        assert app['init_packet_data']['softdevice_req'] == [0x123]
        assert hashlib.sha256(z.read(app['bin_file'])).hexdigest() == expected_bin
    elf = ROOT / '.pio/build' / ENV / 'firmware.elf'
    verified = OUT / 'verified-from-elf.bin'
    subprocess.run([str(ROOT / '.pio-core/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-objcopy.exe'),
                    '-O', 'binary', str(elf), str(verified)], check=True)
    assert hashlib.sha256(verified.read_bytes()).hexdigest() == expected_bin
    from elftools.elf.elffile import ELFFile
    with elf.open('rb') as f:
        e = ELFFile(f)
        symbols = [s.name for s in e.get_section_by_name('.symtab').iter_symbols()
                   if 'backupRecoveryVerified' in s.name]
    old_manifest = json.loads((ROOT / 'logs/hardware-gps-backup-2026-11-03/preflight-manifest.json').read_text())
    assert expected_bin not in [a['sha256'] for a in old_manifest['artifacts']]
    p = identify()
    assert p.device == 'COM8'
    result = dict(passed=True, port=p.device, vid=p.vid, pid=p.pid, serial=p.serial_number,
        base_commit=BASE, uncommitted_patch_sha256=manifest['patch_sha256'],
        zip_sha256=hashlib.sha256(package.read_bytes()).hexdigest(), binary_sha256=expected_bin,
        elf_binary_matches_package=True, recovery_helper_symbols=symbols,
        source_hashes_verified=True, differs_from_original_pr_binary=True,
        selected_environment=ENV, automatic_backup=False, application_only=True,
        external_spi_erase_intended=False,
        upload_method='existing bootloader, Adafruit serial DFU application ZIP, singlebank',
        package=str(package))
    save('candidate-verification.json', result)
    print(json.dumps(result, indent=2), flush=True)
    return result


def validate_flash_identity(text, reference):
    """Validate actual device evidence, never the informational qualification label."""
    if len(reference) != 256 or reference[:4] != b'SFDP':
        raise ValueError('Saved SFDP reference is missing or invalid')
    if text.count('BEGIN_FLASH_ID') != 1 or text.count('END_FLASH_ID') != 1 or 'FLASH ID ERROR' in text:
        raise ValueError('Incomplete or failed flash identity capture')
    ids = re.findall(r'FLASH ID JEDEC ([0-9A-F]+) ([0-9A-F]+)', text)
    if ids != [(f'{i:02X}', '0B4018') for i in range(1, 6)]:
        raise ValueError('External flash identity unavailable, inconsistent or unexpected')
    if 'JEDEC_repeat=PASS SFDP_repeat=PASS' not in text:
        raise ValueError('Flash repetition checks failed')
    status = re.findall(r'FLASH ID SR1 before=([0-9A-F]{2}) after=([0-9A-F]{2}) SR2=([0-9A-F]{2})', text)
    if status != [('00', '00', '02')]:
        raise ValueError('Flash status differs from the previously verified idle/protection state')
    parts = re.findall(r'FLASH ID SFDP pass=([0-9A-F]+) address=([0-9A-F]+) ([0-9A-F]+)', text)
    expected = [(p, a) for p in (1, 2) for a in range(0, 256, 16)]
    if [(int(p, 16), int(a, 16)) for p, a, _ in parts] != expected:
        raise ValueError('SFDP passes are incomplete, duplicated or out of order')
    for _, address, value in parts:
        address = int(address, 16)
        chunk = bytes.fromhex(value)
        if len(chunk) != 16 or chunk != reference[address:address + 16]:
            raise ValueError('SFDP bytes differ from the previously verified device reference')
    return dict(passed=True, jedec='0B4018', sr1_before='00', sr1_after='00', sr2='02',
                sfdp_bytes_per_pass=256, sfdp_passes=2,
                sfdp_header_hex=reference[:8].hex().upper(),
                sfdp_reference_sha256=hashlib.sha256(reference).hexdigest(),
                qualification_label_used_for_admission=False)


class Monitor:
    allowed = {'RECORD STATUS', 'OBS STATUS', 'COMP STATUS', 'TCB STATUS', 'BAT STATUS',
               'GPS STATUS', 'GPS CONFIG', 'GPS RATE', 'GPS BACKUP TEST', 'DIAG ID'}

    def __init__(self, path):
        self.log = path.open('xb')
        self.link = serial.Serial(identify().device, 115200, timeout=.05, write_timeout=1)

    def close(self):
        self.link.close()
        self.log.close()

    def read(self, seconds, marker=None):
        until = time.monotonic() + seconds
        data = bytearray()
        while time.monotonic() < until:
            b = self.link.read(max(1, self.link.in_waiting))
            if b:
                data.extend(b); self.log.write(b); self.log.flush()
            if marker and marker.encode() in data:
                at = data.index(marker.encode())
                if b'\n' in data[at:]:
                    break
        text = data.decode('utf-8', 'replace')
        if marker and marker not in text:
            raise RuntimeError('Serial marker timeout: ' + marker + '\n' + text[-3000:])
        return text

    def command(self, command, marker, seconds=10):
        assert command in self.allowed
        self.log.write(('\nHOST TX ' + command + '\n').encode()); self.log.flush()
        self.link.write((command + '\n').encode()); self.link.flush()
        return self.read(seconds, marker)

    def idle(self):
        states = {}
        for cmd, end in [('RECORD STATUS', 'RECORD STATUS:'), ('OBS STATUS', 'OBS END'),
                         ('COMP STATUS', 'COMP END'), ('TCB STATUS', 'TCB STATUS')]:
            states[cmd] = self.command(cmd, end)
        assert 'RECORD STATUS: STOPPED' in states['RECORD STATUS'], states
        assert 'capture=OFF' in states['OBS STATUS'] and 'RAM_fast_imu=OFF' in states['OBS STATUS'], states
        assert 'capturing=0 fault=0' in states['COMP STATUS'] and 'input=0/2 output=0/2' in states['COMP STATUS'], states
        assert 'state=1 ' in states['TCB STATUS'] and 'error=none' in states['TCB STATUS'] and 'lock=0' in states['TCB STATUS'], states
        return states

    def backup(self, label):
        self.read(.3)
        with (OUT / (label + '.tpx3')).open('xb') as f:
            result = capture(self.link, f, timeout=180)
        data = (OUT / (label + '.tpx3')).read_bytes()
        image, issues = decode_packets(data)
        assert not issues, issues
        (OUT / (label + '-image.bin')).write_bytes(image)
        decoded = decode_image(image)
        for s in decoded['sessions']:
            s.pop('raw', None)
        save(label + '-decoded.json', json_document(decoded))
        result['image_sha256'] = hashlib.sha256(image).hexdigest()
        result['image_bytes'] = len(image)
        result['sessions'] = [dict(id=s['session_id'], status=s['status'], records=len(s['records']))
                              for s in decoded['sessions']]
        result['existing_decode_issues'] = decoded['issues']
        return result


def preflight():
    OUT.mkdir(exist_ok=False)
    result = dict(passed=False, uploaded=False, recording_started=False)
    try:
        result['candidate'] = validate_candidate()
        monitor = Monitor(OUT / 'before-upload-serial.txt')
        try:
            initial = monitor.read(3)
            assert 'TPX3' not in initial, 'An export is already active; stop without commands'
            result['states'] = monitor.idle()
            result['gps'] = monitor.command('GPS STATUS', 'GPS speed_')
            result['battery'] = monitor.command('BAT STATUS', 'BAT source=')
            result['identity'] = monitor.command('DIAG ID', 'END_FLASH_ID', 20)
            result['identity_validation'] = validate_flash_identity(result['identity'],
                (ROOT / 'logs/phase3b4-hardware/sfdp-capture-1.bin').read_bytes())
            result['archive'] = monitor.backup('before-upload')
            result['passed'] = True
            print('Preflight passed; preserved existing sessions:', result['archive']['sessions'], flush=True)
        finally:
            monitor.close()
            result['serial_released'] = True
    finally:
        save('preflight-result.json', result)


def upload():
    pre = json.loads((OUT / 'preflight-result.json').read_text())
    assert pre['passed'] and pre['serial_released']
    result = dict(uploaded=False, application_only=True, external_spi_erase_intended=False,
                  approval='Owner explicitly approved corrected supervised test candidate on COM8; no auto standby/erase/push')
    candidate = validate_candidate()
    with (OUT / 'upload-attempted.json').open('x') as f:
        json.dump(result, f, indent=2)
    try:
        p = identify()
        with serial.Serial(p.device, 1200, timeout=.1) as touch:
            touch.dtr = False
        loader = identify(0x0045, 25)
        result['loader_port'] = loader.device
        result['loader_hwid'] = loader.hwid
        infos = []
        for drive in 'DEGHIJKLMNOPQRSTUVWXYZ':
            path = Path(drive + ':/INFO_UF2.TXT')
            if path.is_file():
                content = path.read_text()
                if 'Seeed' in content and 'nRF52840' in content:
                    infos.append(dict(path=str(path), text=content))
        result['bootloader_info'] = infos
        assert len(infos) == 1 and 'Seeed_XIAO_nRF52840_Sense' in infos[0]['text'], 'Sense bootloader identity not confirmed'
        assert 'S140 version 7.3.0' in infos[0]['text']
        save('bootloader-identity.json', result)
        command = [sys.executable, str(ROOT / '.pio-core/packages/tool-adafruit-nrfutil/adafruit-nrfutil.py'),
                   'dfu', 'serial', '-pkg', candidate['package'], '-p', loader.device, '-b', '115200', '--singlebank']
        result['command'] = command
        print('Verified Sense bootloader; uploading approved application once on', loader.device, flush=True)
        with (OUT / 'upload.txt').open('x') as log:
            p = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=120)
        result['exit_code'] = p.returncode
        assert p.returncode == 0, 'Upload failed; no retry or recovery upload'
        result['uploaded'] = True
        result['application_port'] = identify(0x8044, 30).device
        print('Upload succeeded; application returned on', result['application_port'], flush=True)
    finally:
        save('upload-result.json', result)


if __name__ == '__main__':
    {'preflight': preflight, 'upload': upload}[sys.argv[1]]()
