"""Offline USB disconnect/reconnect and exact TPX3 reconstruction regression."""
import sys,json,hashlib,struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import ROOT,Native,compile
from tcb3_decode import decode_image,decode_packets
seed=(ROOT/'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes()
old=decode_image(seed);assert not old['issues']
elf=compile('tests/reusable/switchless.cpp','reusable-switchless-tests',
    ('tests/recording','tests/flash_write','lib/TinyGPSPlus/src'),(ROOT/'lib/TinyGPSPlus/src/TinyGPS++.cpp',))
results=[]
for case,name in enumerate(('USB start, disconnect, autonomous finalise, reconnect',
    'repeated USB monitor disconnect/reconnect during capture')):
    m=Native(elf);m.call('reusableInit');m.m.mem_write(m.s['SPI'],seed)
    code=m.call('switchlessCase',case,limit=4000000000);assert code==0,(name,code)
    info=struct.unpack('<16I',m.get('switchlessInfo',64))
    image=bytes(m.m.mem_read(m.s['SPI'],info[0]));assert image[:len(seed)]==seed
    doc=decode_image(image);assert not doc['issues'] and len(doc['sessions'])==4
    for a,b in zip(old['sessions'],doc['sessions'][:3]):assert a==b
    s=doc['sessions'][-1];assert s['session_id']==58 and s['status']=='complete' and not s['issues']
    assert s['footer']['committed'][0] in (1000,1001) and s['footer']['committed'][1:]==[100,10,0]
    assert s['footer']['lost']==[0]*4
    assert not any(s['footer'][k] for k in ('rejected','missed','crc_failures','incomplete_writes','full'))
    assert s['raw']==m.get('newWire',info[1])
    times=[r['timestamp_us'] for r in s['records']];assert times==sorted(times)
    imu=[r['timestamp_us'] for r in s['records'] if r['type_id']==1]
    spacing=[b-a for a,b in zip(imu,imu[1:])]
    # Entering fast acquisition rephases the millis scheduler; retain the first
    # measured interval rather than discard/retime an authentic observation.
    assert 9000<=spacing[0]<=11000,spacing[0]
    assert min(spacing[1:])>=9800 and max(spacing[1:])<=10200,(min(spacing[1:]),max(spacing[1:]))
    first=m.get('usbBytes',info[7]);assert m.call('switchlessSecondExport')==0
    info2=struct.unpack('<16I',m.get('switchlessInfo',64));second=m.get('usbBytes',info2[7]);assert first==second
    exported,issues=decode_packets(first);assert not issues and decode_image(exported)['sessions'][-1]['raw']==s['raw']
    results.append(dict(case=name,counts=s['footer']['committed'],stop_elapsed_us=info[2],
        programs=info[3],next_address=hex(info[0]),max_service_us=info[5],max_block_ms=info[6],
        min_imu_spacing_us=min(spacing),max_imu_spacing_us=max(spacing),
        export_bytes=len(first),export_sha256=hashlib.sha256(first).hexdigest()))
    print('PASS:',name,results[-1],flush=True)
    if case==0:(ROOT/'.pio/switchless-example.tpx3').write_bytes(first)
(ROOT/'.pio/switchless-results.json').write_text(json.dumps(results,indent=2)+'\n')
