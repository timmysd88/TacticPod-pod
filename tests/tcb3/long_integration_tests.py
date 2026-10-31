"""Two-minute production acquisition/compressor/store model with CPU stress."""
import sys
sys.dont_write_bytecode=True
from native import ROOT,Native,compile
import json,struct
from tcb3_decode import decode_image
elf=compile('tests/tcb3/long_integration.cpp','tcb3-long-tests',
    ('tests/recording','tests/flash_write','lib/TinyGPSPlus/src'),
    (ROOT/'lib/TinyGPSPlus/src/TinyGPS++.cpp',))
m=Native(elf);code=m.call('integration',7,limit=12000000000)
metrics=struct.unpack('<24I',m.get('integrationInfo',96))
print('LONG result',code,'metrics',metrics,flush=True);assert code==0
image=bytes(m.m.mem_read(m.s['SPI'],metrics[12]));decoded=decode_image(image);session=decoded['sessions'][0]
assert not decoded['issues'] and session['status']=='complete'
assert session['footer']['committed']==[12000,1200,120,0]
raw=m.get('expectedWire',metrics[15]);rows=[];at=0
while at<len(raw):
    n=struct.unpack_from('<H',raw,at+2)[0];rows.append(raw[at:at+n]);at+=n
rows.sort(key=lambda r:(struct.unpack_from('<Q',r,4)[0],0 if r[0]!=1 else 1))
assert session['raw']==b''.join(rows)
(ROOT/'.pio/phase3b3-long.json').write_text(json.dumps(dict(duration_s=120,metrics=metrics,
    committed=session['footer']['committed'],lost=session['footer']['lost'],
    compression_budget_us_per_unit=100,program_busy_ms=10,spi_hz=1000000,imu_read_us=180,
    byte_exact=True),indent=2)+'\n')
print('PASS: sustained nominal-rate high-entropy acquisition, CPU load and busy flash; exact reconstruction')
