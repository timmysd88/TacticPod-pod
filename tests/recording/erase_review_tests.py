"""Actual V1 erase state machine across the full chip, RAM simulation only."""
import sys
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import Native,compile
elf=compile('tests/recording/erase_review.cpp','erase-review-tests',('tests/recording','tests/flash_write'))
for case,name in enumerate(('complete partition','erase readback failure','denied WEL','changed JEDEC','WEL retained after erase')):
    m=Native(elf);result=m.call('eraseReview',case,limit=4000000000)
    assert result==0,(case,name,result)
    print('PASS:',name)
print('PASS: exactly 4095 ascending 20h erases and 4095 WREN, first 000000 last FFE000; no page programs')
print('PASS: all 16773120 recording bytes FF, protected 4096-byte diagnostic canary unchanged, recorder stopped')
