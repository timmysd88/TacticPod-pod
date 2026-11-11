"""Exercise the actual diagnostic USB report using an ARM serial mock."""
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tests/tcb3'))
from native import Native, compile

elf = compile('tests/reusable/boot_serial.cpp', 'boot-serial-tests',
              ('tests/reusable/boot_stub',), ('-Wl,-e,main',))
assert Native(elf).call('main') == 0
print('PASS: diagnostic report waits for USB room, emits once and repeats on request')
