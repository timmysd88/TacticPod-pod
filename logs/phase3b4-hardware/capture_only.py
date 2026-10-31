"""Continue the authorized read-only test after the successful upload.

OBS defaults to passive RAM capture at boot; explicitly stop it before DIAG ID.
No upload, flash mutation or RAM clear command is used here.
"""
import json,pathlib,time
import serial
from serial.tools import list_ports
OUT=pathlib.Path(__file__).resolve().parent
ALLOWED={'RECORD STATUS','GPS STATUS','OBS STOP','OBS STATUS','COMP STOP','COMP STATUS','TCB STATUS','DIAG ID'}
result={'commands':[],'external_flash_mutation_commands_sent':0,'serial_released':False}
def read(p,log,marker=None,seconds=5):
    data=bytearray();end=time.monotonic()+seconds
    while time.monotonic()<end:
        chunk=p.read(max(1,p.in_waiting))
        if chunk:log.write(chunk);log.flush();data.extend(chunk)
        if marker and marker in data and b'\n' in data[data.index(marker):]:
            text=data.decode('utf-8','replace')
            assert not any(s in text for s in ('FLASH ID ERROR','HARDFAULT','HardFault','ASSERT FAILED')),text
            return text
    if marker:raise RuntimeError('Timed out: '+marker.decode()+' received: '+data.decode('utf-8','replace'))
    return data.decode('utf-8','replace')
def command(p,log,cmd,marker):
    assert cmd in ALLOWED;result['commands'].append(cmd)
    log.write(('\nHOST TX '+cmd+'\n').encode());log.flush()
    p.write((cmd+'\n').encode());p.flush()
    return read(p,log,marker.encode(),10)
def statuses(p,log):
    rows={}
    for cmd,marker in [('RECORD STATUS','RECORD STATUS:'),('OBS STATUS','OBS END'),('COMP STATUS','COMP END'),('TCB STATUS','TCB DISABLED')]:
        rows[cmd]=command(p,log,cmd,marker)
    assert 'RECORD STATUS: STOPPED' in rows['RECORD STATUS']
    assert 'capture=OFF' in rows['OBS STATUS'] and 'RAM_fast_imu=OFF' in rows['OBS STATUS']
    assert 'capturing=0' in rows['COMP STATUS'] and 'fault=0' in rows['COMP STATUS']
    return rows
try:
    ports=[p for p in list_ports.comports() if p.serial_number=='0E8A89B7289430BD' and p.vid==0x2886 and p.pid==0x8044]
    assert len(ports)==1,'Expected application device missing'
    with serial.Serial(ports[0].device,115200,timeout=.03,write_timeout=1) as p,(OUT/'capture-transcript.txt').open('wb') as log:
        read(p,log,seconds=.5)
        first=command(p,log,'RECORD STATUS','RECORD STATUS:')
        assert 'RECORD STATUS: STOPPED' in first
        result['obs_stop']=command(p,log,'OBS STOP','OBS END')
        result['comp_stop']=command(p,log,'COMP STOP','COMP END')
        result['before']=statuses(p,log)
        result['gps_before']=command(p,log,'GPS STATUS','GPS speed_')
        assert '10HZ VERIFIED RAM' in result['gps_before']
        for capture in (1,2):
            data=command(p,log,'DIAG ID','END_FLASH_ID')
            assert 'BEGIN_FLASH_ID' in data and 'JEDEC_repeat=PASS' in data and 'SFDP_repeat=PASS' in data,data
            (OUT/f'diag-id-{capture}.txt').write_text(data,encoding='utf-8')
            print(data,flush=True)
        result['after']=statuses(p,log)
        result['gps_after']=command(p,log,'GPS STATUS','GPS speed_')
        assert '10HZ VERIFIED RAM' in result['gps_after']
        result['status']='complete';result['port']=ports[0].device
    print('Serial released; RECORD/OBS/COMP stopped; TCB disabled',flush=True)
except BaseException as exc:
    result['status']='failed';result['error']=repr(exc);raise
finally:
    result['serial_released']=True
    (OUT/'capture-result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
