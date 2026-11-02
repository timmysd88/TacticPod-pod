"""GPS BACKUP TEST: state machine behaviour (host C++) plus source guards for flags, guards and recovery wording."""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[2]
from host_checks import run_cpp
run_cpp('tests/reusable/gps_backup_test.cpp', 'gps-backup-test')

gps = (root / 'src/gps_receiver.cpp').read_text()
power = (root / 'src/gps_power.h').read_text()
ini = (root / 'platformio.ini').read_text()
console = (root / 'src/serial_console.cpp').read_text()
main = (root / 'src/main.cpp').read_text()
docs = (root / 'docs/reusable-prototype.md').read_text()

# Both features default OFF; the test build enables only the command, never automatic backup.
assert '#define TACTICPOD_GPS_BACKUP_TEST 0' in power and '#define TACTICPOD_GPS_BACKUP_STANDBY 0' in power
def env(name):
    m = re.search(r'\[env:%s\](.*?)(?=\n\[|\Z)' % name, ini, re.S)
    assert m, name
    return '\n'.join(l for l in m.group(1).splitlines() if not l.lstrip().startswith(';'))
test_env = env('tacticpod_reusable_usb_gpsbackup')
assert '-DTACTICPOD_GPS_BACKUP_TEST=1' in test_env and 'TACTICPOD_GPS_BACKUP_STANDBY' not in test_env
assert '-DTACTICPOD_GPS_BACKUP_STANDBY=1' in env('tacticpod_reusable_usb_gpsbackup_auto')
for name in ('tacticpod_reusable_usb', 'seeed_xiao_nrf52840_sense'):
    assert 'GPS_BACKUP' not in env(name), 'default environments must not enable GPS backup'

# The command exists only behind the compile-time flag, in the receiver and the menu.
cmd = re.search(r'#if TACTICPOD_GPS_BACKUP_TEST\n  if \(strcmp\(line, "GPS BACKUP TEST"\) == 0\) \{ backupStart\(\); return true; \}', gps)
assert cmd, 'command handler must be inside #if TACTICPOD_GPS_BACKUP_TEST'
assert gps.count('"GPS BACKUP TEST"') == 1
assert re.search(r'#if TACTICPOD_GPS_BACKUP_TEST\n  Serial.println\("  GPS BACKUP TEST', console)

# Entry guards come from the shared refusal() and are evaluated before the cycle starts.
start = re.search(r'static void backupStart\(\)\s*\{.*?\n\}\n', gps, re.S).group(0)
assert 'BackupTest::refusal(g)' in start and 'backupTest.start(g' in start and 'if (!why && !Serial)' in start
guards = re.search(r'backupGuards\(\)\s*\{.*?\n\}\n', gps, re.S).group(0)
for needle in ('Tcb3Session::recording()', 'Tcb3Session::exclusive()', 'Recorder::exclusive()', 'Recorder::exporting()',
               'CompactRecorder::capturing()', 'Tcb3Session::faulted()', 'busy()', 'uartTxReady()', 'recordingReady()'):
    assert needle in guards, needle

# While running or after a failed wake the receiver can never be used to record.
assert re.search(r'bool prepareRecording\(\)\s*\{\s*#if TACTICPOD_REUSABLE\s*if\(backupTestRunning\(\)', gps)
assert 'backupLatched' in re.search(r'bool preparationFailed\(\)\s*\{.*?\n\}\n', gps, re.S).group(0)
assert '!backupLatched && !backupTestRunning()' in re.search(r'bool recordingReady\(\)\s*\{.*?\n\}\n', gps, re.S).group(0)
assert 'backupTestRunning() || startupPending' in gps, 'standby must treat the running test as GPS-busy'
step = re.search(r'static void backupStep\(\)\s*\{.*?\n\}\n', gps, re.S).group(0)
assert 'backupLatched = false' in step and 'currentVerified()' in step, 'latch clears only when NMEA and 10 Hz return'
assert 'backupLatched = t.blocksRecording()' in gps
# backupStep runs before update()'s suspended early return, so the parked UART is always restored.
assert gps.index('  backupStep();\n#endif') < gps.index('if (suspended) return;', gps.index('void update() {'))

# USB stays up: the test never ends or reconfigures USB serial and never resets the receiver.
assert 'Serial.end(' not in gps and 'USBDevice' not in gps
for forbidden in ('0x06, 0x04', 'CFG-RST', 'NVIC_SystemReset', 'GPS RESET'):
    assert forbidden not in gps and forbidden not in power, forbidden
assert 'cannot recover a receiver in standby' in gps and 'power-cycle' in gps
assert gps.count('Serial1.end()') == 2  # suspend() and the test's park step only

# Normal standby only blocks entry while the test runs; the standby policy is not otherwise changed.
assert 'GPS BACKUP' not in main
assert 'backup test' in docs.lower() or 'GPS BACKUP TEST' in docs
print('PASS: GPS BACKUP TEST state machine, default-off flags, entry guards, recording block, USB left untouched')
