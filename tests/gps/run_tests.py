"""Run production GPS + TinyGPSPlus ARM code with mock UART, flash and USB."""
from pathlib import Path
import struct
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tools'))
sys.dont_write_bytecode = True
from tacticpod_decode import decode_record
sys.path.insert(0, str(root / '.pio-core/test-python'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC

out = root / '.pio/gps-tests.elf'
compiler = root / '.pio-core/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-g++.exe'
subprocess.run([str(compiler), '-std=c++11', '-mcpu=cortex-m4', '-mthumb', '-O1',
                '-DARDUINO=10819', '-fno-exceptions', '-fno-rtti', '-fno-unwind-tables',
                '-ffunction-sections', '-fdata-sections', '-nostartfiles',
                '-I' + str(root / 'tests/recording'), '-I' + str(root / 'tests/flash_write'),
                '-I' + str(root / 'lib/TinyGPSPlus/src'),
                str(Path(__file__).with_name('harness.cpp')),
                str(root / 'lib/TinyGPSPlus/src/TinyGPS++.cpp'),
                '-Wl,-Ttext=0x10000', '-Wl,-Tdata=0x20000000', '-Wl,-e,runCase',
                '-Wl,--gc-sections', '-lm', '-o', str(out)], check=True)
with out.open('rb') as stream:
    elf = ELFFile(stream)
    entry = elf.get_section_by_name('.symtab').get_symbol_by_name('runCase')[0]['st_value']
    serial_symbol = elf.get_section_by_name('.symtab').get_symbol_by_name('Serial')[0]['st_value']
    observation_symbol = elf.get_section_by_name('.symtab').get_symbol_by_name('observationWire')[0]['st_value']
    timing_symbol = elf.get_section_by_name('.symtab').get_symbol_by_name('observationTiming')[0]['st_value']
    loads = [(s['p_vaddr'], s.data()) for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
    init = elf.get_section_by_name('.init_array')
    constructors = list(struct.unpack('<' + 'I' * (init.data_size // 4), init.data())) if init else []
names = ['default OFF, 38400 baud, no-UART error on STATUS', 'GP NMEA position/speed/satellites/HDOP/UTC',
         'GN multi-constellation sentences', 'checksum rejection', 'GGA fix loss and missing HDOP',
         'RMC void status invalidates cached fix', 'stale UART/data and message age',
         'one-second output; OFF preserves reception', 'USB backpressure never blocks reception',
         'CSV export suppresses GPS printing but keeps reception', 'incomplete UART sentences return without waiting',
         '100 Hz IMU recording alongside GPS, binary layout unchanged',
         'RAW duration and no-data summary', 'RAW counts NUL/binary bytes and escapes terminal controls',
         'RAW readable NMEA and independent checksum validation', 'RAW invalid checksum rejected',
         'RAW exact end boundary excludes later bytes', 'RAW duplicate/status/OFF do not restart or cancel',
         'RAW backpressure preserves captured data without waiting', 'RAW defers output during CSV export',
         'RAW capture survives USB disconnect', 'RAW overflow counted explicitly',
         'RAW 10-second capture preserves 100 Hz recording', 'RAW timer handles millis wrap',
         'RAW validation excludes pre-capture sentence fragments',
         'explicit loopback PASS, exact binary comparison, parser isolated',
         'no-jumper timeout and explicit successful retry', 'corrupted loopback byte FAIL',
         'pre-existing RX counted and test refused before TX', 'UART/RAW mutual exclusion and duplicate refusal',
         'stuck TX refuses another blocking write', 'loopback result survives USB backpressure',
         'loopback preserves 100 Hz recording', 'loopback timer wrap', 'short TX failure',
         'loopback refused during CSV export', 'GPS reception resumes unchanged after loopback',
         'six exact 3-second scan windows, bytes and sticky error counts, restore 38400',
         'SCAN refused during recording/stopping/erase', 'SCAN blocks concurrent UART/flash/recording commands',
         'SCAN NMEA detection and normal parser resumption', 'SCAN parity-enabled flag reporting',
         'SCAN exposes incorrect physical RX selection', 'SCAN exposes disabled receiver',
         'SCAN stop timeout never falsely claims restoration', 'SCAN start timeout recovers to 38400',
         'SCAN failed receiver restart reports restoration failure', 'SCAN timer wrap',
         'SCAN restores before USB reconnect and resumes reception', 'SCAN startup/flash-test interlocks',
         'SCAN preserves idle IMU and LED without writing flash', 'loopback still works after SCAN',
         'SCAN counts transition discards and clears old error flags',
         'RAW holds a full-rate 38400-baud ten-second capture without omissions',
         'full-rate 38400-baud input and RAW preserve 100 Hz recording',
         'RATE 1 Hz no-fix GGA/RMC counted separately', 'RATE 10 Hz indoors without fix',
         'RATE valid-fix counts at 10 Hz', 'RATE checksum failures excluded',
         'RATE duplicate GP/GN UTC epochs are not double-counted', 'RATE RMC-only output',
         'RATE frozen UTC does not prove 10 Hz epochs', 'RATE missing UTC still counts no-fix messages',
         'RATE no UART data report', 'RATE exact 15-second end boundary', 'RATE USB backpressure',
         'RATE USB disconnect retains results', 'RATE CSV output protection', 'RATE UART diagnostic interlocks',
         'RATE 10 Hz NMEA alongside 100 Hz IMU recording', 'RATE millis and UTC midnight wrap',
         'RATE backwards UTC and duplicate rejection', 'RATE bounded overlong sentence handling',
         'CONFIG decoded RAM settings, MON-VER and golden read-only UBX polls',
         'CONFIG per-key NAK does not poison supported keys', 'CONFIG timeout reports unavailable, never unsupported',
         'CONFIG rejects corrupt checksum then accepts valid response', 'CONFIG validates version/layer/position/key/exact length',
         'CONFIG multiplexes NMEA and ignores NMEA-like data inside UBX', 'CONFIG incremental byte-at-a-time response',
         'CONFIG bounded oversized frame handling', 'CONFIG incomplete frame timeout and NMEA recovery',
         'CONFIG startup and flash-test interlocks', 'CONFIG blocks recording/erase/export and other diagnostics',
         'CONFIG mutually exclusive with RATE/RAW/loopback/SCAN', 'CONFIG retains report under USB backpressure',
         'CONFIG completes collection without USB and reports after reconnect', 'CONFIG stuck TX never writes again',
         'CONFIG short TX reported safely', 'CONFIG timers survive millis wrap',
         'CONFIG followed by intact 100 Hz recording; refused while recording', 'loopback still works after CONFIG',
         'SCAN still works after CONFIG', 'CONFIG absolute frame timeout under continuous corrupt traffic',
         'CONFIG request deadline and stale-key rejection',
         'CONFIG reads OTP clock verification profile without programming', 'CONFIG unsupported OTP clock fields remain unavailable',
         'CONFIG duplicate-key reply values must agree', 'CONFIG alternating TX lengths reject stale DMA completion',
         'RAW still counts every UBX byte after CONFIG', 'CONFIG MON-VER bounds and terminal-control sanitisation',
         'PROFILE atomic RAM-only 10Hz test and exact verified restore', 'PROFILE recording/startup/flash interlocks',
         'PROFILE RESTORE without snapshot sends nothing', 'PROFILE requires advertised SPG 5.10 firmware',
         'PROFILE snapshot NAK aborts before writes', 'PROFILE snapshot timeout aborts before writes',
         'PROFILE rejected commit automatically restores', 'PROFILE lost staging ACK cancels and restores',
         'PROFILE lost commit ACK rolls back actual applied values', 'PROFILE mismatched apply readback rolls back',
         'PROFILE apply verification timeout automatically restores', 'PROFILE failed automatic rollback retains snapshot for retry',
         'PROFILE lost restore commit ACK verified by values', 'PROFILE repeat TEST cannot replace original snapshot',
         'PROFILE UART/recording/erase command interlocks', 'PROFILE USB backpressure does not delay collection',
         'PROFILE USB disconnect preserves snapshot and completion', 'PROFILE timers survive millis wrap',
         'PROFILE corrupt/wrong-layer/wrong-key replies cannot satisfy reads', 'PROFILE incompatible UART preflight sends no writes',
         'PROFILE RESTORE refused while recording', 'PROFILE lost ongoing-transaction ACK recovered',
         'PROFILE short UART write automatically restores', 'PROFILE NAK in ongoing transaction restores',
         'PROFILE preserves SBAS and restores original disabled QZSS', 'PROFILE 10Hz GPS alongside unchanged 100Hz IMU recording',
         'PROFILE failed explicit restore retains snapshot and succeeds on retry', 'PROFILE fragmented UBX and simultaneous NMEA',
         'PROFILE stuck TX reports unverified rollback without blocking; later explicit restore succeeds',
         'PROFILE refuses already-active test profile without its old snapshot', 'PROFILE power-save preflight refuses without changing settings',
         'PROFILE refused during CONFIG/RATE/RAW/loopback/SCAN', 'PROFILE operation deadline automatically rolls back',
         'PROFILE automatic rollback completes with USB disconnected', 'PROFILE restore readback timeout retains snapshot until verified retry',
         'REGRESSION SPG 5.10 ignores old APPLY payload; reproduces MEAS staying 1000',
         'PROFILE golden U2 100ms/1000ms bytes and START/CONTINUE/CONTINUE/empty APPLY',
         'PROFILE preserves actual 1000ms mismatch before automatic rollback',
         'PROFILE deferred diagnostics retain original mismatch under USB backpressure',
         'PROFILE commit NAK and transaction status survive rollback',
         'PROFILE commit timeout status survives successful rollback',
         'PROFILE duplicate ACK is logged and does not advance transaction',
         'PROFILE readback timing includes 600ms commit settle and prior signal reads',
         'PROFILE bounded diagnostic overflow cannot erase first mismatch evidence',
         'STARTUP applies tested RAM profile, reports status and restores without reapplying',
         'STARTUP missing communication times out without writes',
         'STARTUP already configured receiver does not invent original snapshot',
         'STARTUP completes without USB, releases interlock and preserves restore',
         'STARTUP lost commit ACK restores original settings',
         'STARTUP retries a lost snapshot reply safely',
         'STARTUP defers during recording and preserves IMU acquisition',
         'STARTUP unanswered UBX reads stop after exactly three attempts',
         'RATE 10Hz valid RMC primary with independent 1Hz supporting GGA',
         'RATE 149 valid RMC epochs at 9.933Hz passes window tolerance',
         'RATE both valid RMC and GGA at 1Hz fail 10Hz cadence',
         'RATE missing UTC no-fix RMC stays UNKNOWN despite GGA UTC',
         'RATE advancing no-fix RMC establishes cadence without valid navigation claim',
         'RATE frozen RMC UTC stays UNKNOWN despite advancing GGA',
         'RATE valid-status RMC without UTC cannot establish 10Hz cadence',
         'OBS RMC field-scoped fixed-point wire bytes and shared microsecond timestamps',
         'OBS void RMC clears all unavailable fields despite cached valid position',
         'OBS mode N explicitly invalidates RMC A coordinates',
         'OBS independent GGA quality, altitude, HDOP and no-fix observation',
         'OBS missing RMC UTC does not inherit a previous timestamp',
         'OBS checksum failure excluded and subsequent valid sentence recovered',
         'OBS malformed ranges/date/time/speed/course produce explicit missing masks',
         'OBS bounded overlong/partial/UBX frames cannot leak observations',
         'OBS byte-at-a-time completion stamps final LF, including valid zero position',
         'OBS bounded queues retain immutable copies, count overflow and clear RAM only',
         'CLOCK pending wrap extends exactly once',
         'CLOCK ISR extends hardware counter without main-loop polling',
         'CLOCK capture/wrap race cannot return a backwards or double-extended timestamp',
         'CLOCK preserves IRQ mask and idempotent boot initialization',
         'OBS GGA associates forward midnight/year rollover date',
         'OBS GGA associates previous-day sentence delivered after midnight RMC',
         'OBS stale/missing UTC anchor cannot date a GGA sentence',
         'OBS 15s concurrent 100Hz IMU/10Hz RMC/1Hz GGA retains intact V1 recording',
         'OBS START supports 100Hz RAM-only acquisition with zero flash writes',
         'OBS USB backpressure never blocks parsing or overfills TX',
         'OBS controls/reporting refused quietly during CSV export',
         'OBS STOP cannot stop or alter independent 100Hz V1 recording',
         'OBS signed extremes, quality bounds and scaled numeric limits',
         'OBS unsupported leap-second UTC creates a V2-compatible EVENT',
         'OBS GPS silence leaves IMU acquisition running without invented GPS samples',
         'OBS duplicate/backwards UTC and invalid fixes are preserved independently',
         'OBS partial sentence expires and resynchronises without stale data',
         'OBS saturated RAM queues do not lose or change any V1 recorded samples',
         'OBS bounded EVENT overflow counts rejected leap-second events',
         'OBS IMU timestamp precedes all six simulated sensor reads',
         'COMP RAM-only concurrent 100Hz IMU/10Hz RMC/1Hz GGA with OBS stopped',
         'COMP concurrent V1 recording commits all 1500 samples with no missed deadlines',
         'COMP output backpressure faults RAM capture without changing V1 recording',
         'COMP controls and diagnostics remain silent during V1 CSV export']
for index, name in enumerate(names):
    machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    machine.mem_map(0x10000, 0x100000)
    machine.mem_map(0x20000000, 0x200000)
    for address, data in loads:
        machine.mem_write(address, data)
    for function in constructors + [entry]:
        machine.reg_write(UC_ARM_REG_SP, 0x201FF000)
        machine.reg_write(UC_ARM_REG_LR, 0xF0001)
        machine.reg_write(UC_ARM_REG_R0, index)
        machine.emu_start(function | 1, 0xF0000, count=100000000)
        if machine.reg_read(UC_ARM_REG_PC) != 0xF0000:
            raise SystemExit(f'FAIL: {name} (instruction budget exhausted)')
    result = machine.reg_read(UC_ARM_REG_R0)
    if result:
        diagnostic = bytes(machine.mem_read(serial_symbol + 8, 12000)).split(b'\0', 1)[0]
        print(diagnostic.decode('utf-8', 'replace'))
        raise SystemExit(f'FAIL: {name} (assertion {result})')
    if index == 160:
        evidence = bytes(machine.mem_read(observation_symbol, 108))
        imu = decode_record(evidence[:24])
        rmc = decode_record(evidence[24:68])
        gga = decode_record(evidence[68:108])
        assert imu['timestamp_us'] == 100323 and imu['axes_raw'] == [-32768, 32767, 0, 1, -1, 123]
        assert rmc['timestamp_us'] == 100123 and rmc['fix_valid'] and rmc['utc']['date'] == '2026-10-30'
        assert rmc['fields']['speed_mm_s'] == 11524 and rmc['fields']['course_e5_deg'] == 8440000
        assert gga['timestamp_us'] == 100223 and gga['hdop'] == 1.28 and gga['altitude_msl_mm'] == -1200
    if index == 183:
        event = decode_record(bytes(machine.mem_read(observation_symbol, 32)))
        assert event['code'] == 5 and event['argument'] == 2 and event['stream'] == 2
    if index in (177, 178, 184):
        ni, nr, low_i, high_i, low_r, high_r = struct.unpack('<6Q', bytes(machine.mem_read(timing_symbol, 48)))
        print(f'SIM TIMING: IMU={ni} gap_us={low_i}..{high_i}; RMC={nr}' +
              (f' gap_us={low_r}..{high_r}' if nr else ' (GPS silent)'))
    print('PASS:', name)
