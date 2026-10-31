import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import Native,compile
elf=compile('tests/reusable/console.cpp','reusable-console-tests',('tests/recording','tests/flash_write'))
m=Native(elf);code=m.call('firstConsole');assert code==0,code
assert b'BAT STATUS' in m.get('Serial',12000)
print('PASS: reusable actual USB parser and internal diagnostic guard reject all erases/V1 writes; boot never records')
