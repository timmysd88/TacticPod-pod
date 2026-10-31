"""User-authorized one-shot partition erase. No upload/start/diagnostic erase.

Persistent attempted.json prevents accidentally repeating the destructive
sequence by re-running this script. Only status/ID reads occur outside the
exact RECORD ERASE / RECORD ERASE CONFIRM pair.
"""
import json,pathlib,re,time,hashlib
import serial
from serial.tools import list_ports
ROOT=pathlib.Path(__file__).resolve().parents[2]
OUT=pathlib.Path(__file__).resolve().parent
assert not (OUT/'attempted.json').exists(),'Erase already attempted: never repeat automatically'
ALLOWED={'RECORD STATUS','OBS STATUS','COMP STATUS','TCB STATUS','DIAG ID','RECORD ERASE','RECORD ERASE CONFIRM'}
result={'erase_started':False,'erase_completed':False,'serial_released':False,'commands':[],'errors':[],
        'sector_count_confirmed':0,'diagnostic_sector_independent_byte_comparison':False,
        'diagnostic_sector_excluded_by_reviewed_address_guards':True}
def read_for(p,log,seconds,marker=None):
    data=bytearray();end=time.monotonic()+seconds
    while time.monotonic()<end:
        b=p.read(max(1,p.in_waiting))
        if b:log.write(b);log.flush();data.extend(b)
        if marker and marker.encode() in data and b'\n' in data[data.index(marker.encode()):]:break
    text=data.decode('utf-8','replace')
    if marker and marker not in text:raise RuntimeError('response timeout: '+marker+' received '+text)
    return text
def command(p,log,cmd,marker):
    assert cmd in ALLOWED
    result['commands'].append(cmd);log.write(('\nHOST TX '+cmd+'\n').encode());log.flush()
    p.write((cmd+'\n').encode());p.flush();return read_for(p,log,10,marker)
def stopped(p,log):
    r={}
    for cmd,mark in [('RECORD STATUS','RECORD STATUS:'),('OBS STATUS','OBS END'),('COMP STATUS','COMP END'),('TCB STATUS','TCB DISABLED')]:r[cmd]=command(p,log,cmd,mark)
    assert 'RECORD STATUS: STOPPED' in r['RECORD STATUS'],r
    assert 'capture=OFF' in r['OBS STATUS'] and 'RAM_fast_imu=OFF' in r['OBS STATUS'],r
    assert 'capturing=0 fault=0' in r['COMP STATUS'] and 'input=0/2 output=0/2' in r['COMP STATUS'],r
    return r
def identity(p,log):
    t=command(p,log,'DIAG ID','END_FLASH_ID')
    assert re.findall(r'FLASH ID JEDEC \d+ ([0-9A-F]+)',t)==['0B4018']*5,t
    assert 'SR1 before=00 after=00 SR2=02' in t and 'JEDEC_repeat=PASS SFDP_repeat=PASS' in t,t
    reference=(ROOT/'logs/phase3b4-hardware/sfdp-capture-1.bin').read_bytes()
    chunks=re.findall(r'FLASH ID SFDP pass=(\d+) address=([0-9A-F]+) ([0-9A-F]+)',t)
    assert len(chunks)==32
    for _,a,h in chunks:
        a=int(a,16);assert bytes.fromhex(h)==reference[a:a+16]
    assert int.from_bytes(reference[0x34:0x38],'little')+1==134217728
    return t
try:
    baseline=json.loads((ROOT/'logs/phase3b5-review/source-baseline.json').read_text())
    assert all(hashlib.sha256((ROOT/p).read_bytes()).hexdigest()==h for p,h in baseline.items())
    elf=ROOT/'.pio/build/seeed_xiao_nrf52840_sense/firmware.elf'
    assert hashlib.sha256(elf.read_bytes()).hexdigest()=='a83cbaabf9c0e18034a5f70a987539c77c574e429e9b6aa3a06caf6d1a836089'
    ports=[p for p in list_ports.comports() if p.serial_number=='0E8A89B7289430BD' and p.vid==0x2886 and p.pid==0x8044]
    assert len(ports)==1
    with serial.Serial(ports[0].device,115200,timeout=.05,write_timeout=1) as p,(OUT/'erase-transcript.txt').open('wb') as log:
        read_for(p,log,.5)
        result['before']=stopped(p,log);result['identity_before']=identity(p,log)
        (OUT/'attempted.json').write_text(json.dumps({'action':'single reviewed recording-partition erase','first_address':'000000','last_address':'FFE000','exclusive_end':'FFF000','no_automatic_repeat':True},indent=2)+'\n')
        armed=command(p,log,'RECORD ERASE','Within 30 seconds')
        assert '0x000000-0xFFEFFF' in armed and 'Diagnostic sector is preserved.' in armed,armed
        began=time.monotonic()
        ack=command(p,log,'RECORD ERASE CONFIRM','Confirmed recording-area sector erase started.')
        result['erase_started']=True;print('One-shot recording partition erase started.',flush=True)
        next_status=time.monotonic()+10;next_notice=time.monotonic()+45;pending=''
        completed_line='RECORD erase complete; diagnostic sector untouched. Recording remains OFF.'
        while True:
            text=read_for(p,log,.2)
            pending+=text
            if any(s in pending for s in ('RECORD ERROR:','RECORD STATUS: FAULT','HardFault','HARDFAULT','ASSERT FAILED','RECORD scan complete')):
                raise RuntimeError('Fault/reset during erase: '+pending)
            if completed_line in pending:
                result['erase_completed']=True;result['sector_count_confirmed']=4095
                result['verified_ff_bytes']=0xfff000;result['erase_elapsed_seconds']=time.monotonic()-began
                print('All 4095 sectors erased and readback-verified by firmware; elapsed %.1f s'%result['erase_elapsed_seconds'],flush=True)
                break
            if time.monotonic()>=next_status:
                status=command(p,log,'RECORD STATUS','RECORD STATUS:')
                if completed_line in status:
                    pending+=status;continue
                assert 'RECORD STATUS: ERASING' in status,'Unexpected erase state: '+status
                result['last_status']=status;next_status=time.monotonic()+10
            if time.monotonic()>=next_notice:
                print('Erase active %.0f s; firmware verifies each sector before advancing; no error reported.'%(time.monotonic()-began),flush=True)
                next_notice=time.monotonic()+45
            if time.monotonic()-began>7200:
                raise RuntimeError('Host monitoring deadline exceeded; no retry/reset/erase sent')
            pending=pending[-512:]
        result['after']=stopped(p,log)
        assert 'next_sector=0x0' in result['after']['RECORD STATUS']
        result['identity_after']=identity(p,log)
        assert result['identity_before']==result['identity_after'],'Identity/status/SFDP differs after erase'
        result['status']='complete'
    print('Final states STOPPED/OFF/OFF/DISABLED; COM8 released.',flush=True)
except BaseException as exc:
    result['status']='failed';result['errors'].append(repr(exc));raise
finally:
    result['serial_released']=True
    (OUT/'result.json').write_text(json.dumps(result,indent=2)+'\n')
