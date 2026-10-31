import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import Native,compile
elf=compile('tests/reusable/policy.cpp','battery-policy-tests')
for i,name in enumerate(('USB only','uncalibrated cell','calibrated but absent',
    'warning hysteresis','low dwell and recovery','hard threshold and outlier',
    'invalid readings','staleness','configuration and divider','backward timestamp',
    'single outlier','USB mode clears battery actions','duplicate timestamps cannot confirm low voltage',
    'low initial voltage refuses start before dwell expires','startup median rejects a single low outlier',
    'instantaneous low refuses start without falsely stopping an existing session')):
    m=Native(elf);code=m.call('policyCase',i);assert code==0,(name,code)
    print('PASS:',name,flush=True)
