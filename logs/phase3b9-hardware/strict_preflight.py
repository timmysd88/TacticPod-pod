from common import *
import hashlib,sys
from serial.tools import list_ports
sys.path.insert(0,str(ROOT/'tools'))
from tcb3_decode import decode_packets,decode_image
result={'start_commands_sent':0,'recording_started':False,'physical_flash_writes':False,'serial_released':False,'decision':'NO-GO under strict non-FF/address-mismatch abort conditions','blocker':'Reviewed profile 2 Probe skips non-FF candidates; header scan can move the start beyond 0x2000. No mandatory start-address equality or host confirmation gate before capture/programming.'}
c=None
try:
    assert not (OUT/'record-attempted.json').exists(),'A recording attempt already exists'
    checks=json.loads((ROOT/'logs/phase3b8-review/final-source-hashes.json').read_text())
    assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in checks.items())
    tests=json.loads((ROOT/'logs/phase3b8-review/regressions.json').read_text())
    assert len(tests)==18 and all(t['exit_code']==0 for t in tests.values())
    result['reviewed_source_hashes_match']=True;result['regression_suites_passed']=18
    ports=[p for p in list_ports.comports() if p.serial_number==SERIAL_NUMBER and p.vid==0x2886 and p.pid==0x8044]
    assert len(ports)==1
    c=Link(ports[0].device,'strict-conditions-transcript.txt');c.read(.5)
    result['states']=c.states()
    assert 'next_sector=0x2000' in result['states']['RECORD STATUS']
    result['identity']=c.identity()
    result['gps']=c.command('GPS STATUS','GPS speed_')
    assert '10HZ VERIFIED RAM' in result['gps']
    data=c.export('strict-conditions-before.tpx3')
    assert data==(ROOT/'logs/phase3b7-hardware/first-write-1.tpx3').read_bytes()
    image,issues=decode_packets(data);decoded=decode_image(image)
    assert not issues and not decoded['issues'] and len(decoded['sessions'])==1
    s=decoded['sessions'][0]
    assert s['status']=='complete' and not s['issues'] and s['sectors']==[0,4096]
    assert s['footer']['committed']==[500,50,5,0] and s['footer']['lost']==[0]*4
    assert s['raw']==(ROOT/'logs/phase3b7-hardware/first-write.v2wire').read_bytes()
    result['old_export']={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'archive_byte_identical':True,'complete':True,'counts':[500,50,5,0]}
    result['final_states']=c.states()
    result['full_allocation_independently_prescanned']=False
    print('PASS: archived session unchanged, states stopped, identity and GPS verified. No TCB START sent: strict abort condition not enforced by installed firmware.',flush=True)
except BaseException as exc:
    result['error']=repr(exc);raise
finally:
    if c:c.close();result['commands']=c.commands
    result['serial_released']=True;save('strict-conditions-result.json',result)
    print('COM8 released.',flush=True)
