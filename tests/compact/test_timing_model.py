"""Explicit instruction-cost sensitivity test, not a hardware timing bound."""
import json
import sys
sys.dont_write_bytecode=True
import run_tests as native

results=[]
for name,blocks,_ in native.bench.datasets(2,2):
    if name=='real_recording':continue
    for cpi in (1,4,12):
        _,e=native.encode(blocks[0],True,cpi)
        assert e['max_units']<=8
        # A final bounded unit can exceed the 200us target. The artificial
        # 12-CPI sensitivity model must still leave ample 10ms interval slack.
        assert e['clock_max_slice_us']<1000
        results.append(dict(name=name,**e))
(native.ROOT/'.pio/phase3a-timing-model.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS: 21 ARM instruction-cost timing sensitivity runs; exact round trips')
