import sys
sys.dont_write_bytecode=True
from native import ROOT,Native,compile
elf=compile('tests/tcb3/console.cpp','tcb3-console-tests',('tests/recording','tests/flash_write'))
for fast in (0,1):
    m=Native(elf);result=m.call('bulkReadCase',fast)
    assert result==0,(fast,result)
print('PASS: bounded bulk SPI reads preserve data, address, transaction and 1/4 MHz clocks')
m=Native(elf);result=m.call('consoleCase')
assert result==0,result
print('PASS: real USB command parser can STOP/STATUS while TCB owns flash; competing writes refused')
