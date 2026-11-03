"""One explicitly approved upload and bounded GPS tests; never starts/erases recording."""
from pathlib import Path
import hashlib
import importlib.util
import json
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).with_name('hardware-run-approved-20261103')
APPROVED_BIN = 'aced1874981173ebfbb3904390f6088214aca3419f72f6a5fae0deb4f4c14051'
spec = importlib.util.spec_from_file_location('hw', Path(__file__).with_name('hardware_run.py'))
hw = importlib.util.module_from_spec(spec)
# hardware_run's optional run label is not an input to this wrapper.
argv = sys.argv[:]
sys.argv = [sys.argv[0]]
spec.loader.exec_module(hw)
sys.argv = argv
hw.OUT = OUT


def save(name, value):
    hw.save(name, value)


def gps_status(monitor, initial=False):
    deadline = time.monotonic() + (60 if initial else 10)
    while time.monotonic() < deadline:
        monitor.log.write(b'\nHOST TX GPS STATUS\n'); monitor.log.flush()
        monitor.link.write(b'GPS STATUS\n'); monitor.link.flush()
        text = monitor.read(2)
        if 'GPS speed_' in text:
            assert '38400 | NMEA=VALID' in text, text
            if initial:
                assert 'GPS configuration: CURRENT RAM readback verified: 100ms / RMC 10Hz / GGA 1Hz' in text, text
            return text
        if not initial or 'GPS profile operation active' not in text:
            raise RuntimeError('GPS status incomplete/refused; stop: ' + text)
    raise RuntimeError('GPS startup audit never became ready; stop without recovery')


def flash_id(monitor):
    text = monitor.command('DIAG ID', 'END_FLASH_ID', 20)
    validation = hw.validate_flash_identity(text, (ROOT/'logs/phase3b4-hardware/sfdp-capture-1.bin').read_bytes())
    return dict(response=text, validation=validation)


def prepare():
    OUT.mkdir(exist_ok=False)
    result = dict(passed=False, serial_released=False)
    monitor = None
    try:
        candidate = hw.validate_candidate()
        assert candidate['binary_sha256'] == APPROVED_BIN
        assert candidate['application_only'] and not candidate['external_spi_erase_intended']
        assert not candidate['automatic_backup']
        result['candidate'] = candidate
        monitor = hw.Monitor(OUT/'pre-upload-serial.txt')
        result['initial'] = monitor.read(10)
        assert 'TPX3' not in result['initial'] and 'BEGIN_RECORD_CSV' not in result['initial']
        result['states'] = monitor.idle()
        result['gps'] = gps_status(monitor, initial=True)
        result['flash'] = flash_id(monitor)
        result['archive'] = monitor.backup('before-upload')
        prior = ROOT/'logs/hardware-gps-backup-2026-11-03/final-readonly-preflight-20261103-091526/recordings-readonly-image.bin'
        assert (OUT/'before-upload-image.bin').read_bytes() == prior.read_bytes(), 'Recordings changed since final preflight; stop for review'
        result['final_states'] = monitor.idle()
        result['passed'] = True
        print('PASS: approved BIN hash exact; application-only DFU, no intentional external SPI erase.', flush=True)
        print('PASS: same COM8, idle, NMEA/profile ready, flash ID/SFDP and recording image unchanged.', flush=True)
    finally:
        if monitor:
            monitor.close()
        result['serial_released'] = True
        save('preflight-result.json', result)


def upload():
    # Re-use the previously inspected application-only serial DFU path. One
    # exclusive attempt marker prevents repeating this upload accidentally.
    candidate = json.loads((OUT/'candidate-verification.json').read_text())
    assert candidate['binary_sha256'] == APPROVED_BIN
    hw.upload()


def run_cycle(monitor, number):
    cycle = dict(number=number, passed=False)
    save(f'cycle-{number}.json', cycle)
    print('Starting supervised GPS BACKUP TEST cycle', number, flush=True)
    monitor.log.write(f'\nHOST TX GPS BACKUP TEST [cycle {number}]\n'.encode()); monitor.log.flush()
    monitor.link.write(b'GPS BACKUP TEST\n'); monitor.link.flush()
    deadline = time.monotonic() + 90
    output = ''
    while time.monotonic() < deadline:
        output += monitor.read(.5)
        if 'GPS BACKUP TEST refused:' in output:
            cycle.update(response=output, error='Test refused; stop without another cycle')
            save(f'cycle-{number}.json', cycle)
            raise RuntimeError(cycle['error'] + '\n' + output)
        if 'GPS BACKUP TEST END\n' in output.replace('\r', ''):
            break
    else:
        cycle.update(response=output, error='Incomplete recovery/test timeout; stop without recovery')
        save(f'cycle-{number}.json', cycle)
        raise RuntimeError(cycle['error'])
    cycle['response'] = output
    save(f'cycle-{number}.json', cycle)
    required = ['GPS BACKUP TEST: PASS', 'standby request sent: YES',
                'standby confirmed (UART silent 1500 ms after 3000 ms parked): YES',
                'UART wake attempted: YES', 'GPS baud 38400: CONFIRMED', '10 Hz configuration: VERIFIED']
    if not all(s in output for s in required):
        cycle['error'] = 'GPS backup did not pass all recovery requirements'
        save(f'cycle-{number}.json', cycle)
        raise RuntimeError(cycle['error'] + '\n' + output)
    timing = re.search(r'time until valid NMEA: (\d+) ms', output)
    assert timing, output
    cycle['time_to_valid_nmea_ms'] = int(timing[1])
    cycle['gps_status'] = gps_status(monitor, initial=True)
    cycle['states'] = monitor.idle()
    cycle['flash'] = flash_id(monitor)
    rate = monitor.command('GPS RATE', 'GPS RATE END', 23)
    cycle['gps_rate'] = rate
    save(f'cycle-{number}.json', cycle)
    assert 'output_cadence_10hz=YES' in rate and 'UART=38400' in rate, rate
    assert 'checksum_failed=0' in rate, rate
    cycle['final_gps_status'] = gps_status(monitor)
    cycle['final_states'] = monitor.idle()
    cycle['passed'] = True
    cycle['recording_readiness_evidence'] = ('PASS transitions to Done and clears backup latch; fresh NMEA and verified profile observed. '
        'No recording start probe; console exposes no direct readiness flag. Acceptance of a following cycle also requires recordingReady().')
    save(f'cycle-{number}.json', cycle)
    print('Cycle', number, 'PASS; NMEA after', timing[1], 'ms; 10Hz cadence and flash verified.', flush=True)
    return cycle


def tests():
    upload_result = json.loads((OUT/'upload-result.json').read_text())
    assert upload_result['uploaded'] and upload_result['exit_code'] == 0
    result = dict(passed=False, uploaded=True, cycles=[], recording_started=False,
                  erases=False, automatic_gps_standby=False, extra_uploads=False)
    monitor = None
    try:
        monitor = hw.Monitor(OUT/'post-upload-and-cycles-serial.txt')
        result['startup'] = monitor.read(12)
        assert 'GPS BACKUP TEST - stopped-only' in result['startup'], 'Supervised-test command missing from startup menu'
        result['gps_startup'] = gps_status(monitor, initial=True)
        result['idle_startup'] = monitor.idle()
        result['flash_startup'] = flash_id(monitor)
        save('test-result.json', result)
        print('Startup PASS: serial 115200, idle, valid NMEA/10Hz RAM profile, flash/SFDP normal.', flush=True)
        for number in range(1, 4):
            try:
                cycle = run_cycle(monitor, number)
            except Exception:
                result['cycles'].append(json.loads((OUT/f'cycle-{number}.json').read_text()))
                raise
            result['cycles'].append(cycle)
            save('test-result.json', result)
        result['recordings_after'] = monitor.backup('after-cycles')
        before = (OUT/'before-upload-image.bin').read_bytes()
        after = (OUT/'after-cycles-image.bin').read_bytes()
        assert after == before, 'Recording contents changed; stop without modifications'
        result['recording_image_byte_identical'] = True
        result['final_idle'] = monitor.idle()
        result['passed'] = True
        print('All 3 cycles PASS; external recordings byte-identical before upload and after cycles.', flush=True)
    except Exception as exc:
        result['error'] = str(exc)
        print('STOPPED:', exc, flush=True)
        raise
    finally:
        if monitor:
            monitor.close()
        result['serial_released'] = True
        save('test-result.json', result)


if __name__ == '__main__':
    {'prepare': prepare, 'upload': upload, 'tests': tests}[sys.argv[1]]()
