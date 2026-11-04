import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import Native,compile
elf=compile('tests/reusable/adc.cpp','battery-real-backend-tests',('tests/reusable/hw',))
for i,name in enumerate(('real-backend register sequencing','ADC timeout without blocking',
    'existing ADC owner','existing ADC IRQ owner','rail reading invalid',
    'VBUS independent of serial','operator calibration and arming',
    'burst trimmed mean rejects a wild conversion','noisy burst discarded',
    'untrusted status reports UNKNOWN and honest flags')):
    m=Native(elf);code=m.call('adcCase',i);assert code==0,(name,code)
    print('PASS:',name,flush=True)
