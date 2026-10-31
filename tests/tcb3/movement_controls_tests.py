"""Production USB command guards in profile 3; simulated flash only."""
from native import ROOT, Native, compile
elf = compile('tests/tcb3/movement_console.cpp', 'tcb3-movement-console-tests',
              ('tests/recording', 'tests/flash_write'))
m = Native(elf)
assert m.call('firstConsole') == 0
print('PASS: profile 3 actual USB parser and diagnostic entry block erases/V1 writes; disconnected boot never records')
