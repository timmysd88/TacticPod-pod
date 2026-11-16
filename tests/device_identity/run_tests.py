"""Identity and transport ownership regressions; ARM host simulation, no hardware."""
import json,struct,sys
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tcb3'))
from native import ROOT,Native,compile
from tcb3_decode import decode_image,decode_packets
includes=('tests/recording','tests/flash_write','lib/TinyGPSPlus/src')
extra=(ROOT/'lib/TinyGPSPlus/src/TinyGPS++.cpp',)
def build(name,inc=includes,ext=()):
    return compile('tests/device_identity/'+name+'.cpp','identity-'+name,inc,ext)
def cstr(m,name,n):return m.get(name,n).split(b'\0')[0].decode()
elf=build('format',(),('--specs=nosys.specs',))
values=(0,1,2**64-1,2**53+1,0x0e8a89b7289430bd,0x0e8a89b8289430bd)
for index,value in enumerate(values):
    m=Native(elf);assert m.call('identityFormat',index)==0
    doc=json.loads(cstr(m,'identityJson',192))
    assert doc==dict(type='device_info',version=1,uid_hex=f'{value:016x}',device_id=str(value),scheme='nrf52840-ficr64-v1',firmware='ble-download-2')
    assert cstr(m,'identityHex',17)==f'{value:016x}' and cstr(m,'identityDecimal',21)==str(value)
    name=cstr(m,'identityName',19);assert name=='TacticPod-'+f'{value:016x}'[-8:] and len(name)+2<=31
    assert name.startswith('TacticPod')
    metadata=m.get('identityMetadata',72)
    assert metadata[16:24]==struct.pack('<Q',value) and struct.unpack_from('<I',metadata,68)[0]==1
print('PASS: six UID patterns, leading zeros, exact uint64 decimal, bounded JSON, legacy metadata encoding and display collision semantics',flush=True)
elf=build('console')
for case in range(8):
    result=Native(elf).call('identityConsole',case);assert not result,('console',case,result)
print('PASS: real console parser, full identity, idle-only guards, backpressure, command cancellation, timeout, disconnect and clock wrap',flush=True)
elf=build('ble',('tests/reusable/ble_transport','tests/flash_write'),('--specs=nosys.specs','-Wall','-Wextra','-Werror'))
for device in range(2):
    result=Native(elf).call('identityBle',device);assert not result,('BLE',device,result)
for scenario in range(2):
    result=Native(elf).call('identityBleFailure',scenario);assert not result,('BLE identity failure',scenario,result)
print('PASS: actual BLE initialisation; read-only custom UID on the existing service, exactly 16 ASCII bytes, unchanged DIS/protocol, suffix collisions and fail-closed initialisation',flush=True)
elf=build('metadata',ext=extra)
m=Native(elf);result=m.call('firstWrite',0);assert not result,('metadata',result)
doc=decode_image(bytes(m.m.mem_read(m.s['SPI'],65536)))
assert not doc['issues']
s=doc['sessions'][0]
assert not s['issues'] and str(s['metadata']['device_id'])==str(values[4]),s['metadata']
assert s['metadata']['accel_ug_per_lsb']==244 and s['metadata']['gyro_udps_per_lsb']==35000
print('PASS: real recording creation and TPX3 decoding preserve full factory UID and sensor metadata',flush=True)
elf=build('legacy')
exports=[]
for inject in range(2):
    m=Native(elf);result=m.call('identityLegacy',inject);assert not result,('legacy',inject,result)
    exports.append(m.get('legacyIdentityCsv',m.word('legacyIdentitySize')))
assert exports[0]==exports[1]
print('PASS: legacy recording rejects identity; repeated identity requests leave CSV export byte-identical',flush=True)
elf=build('binary',ext=extra)
seed=(ROOT/'tests/reusable/fixtures/physical-three-sessions.bin').read_bytes();exports=[]
for inject in range(2):
    m=Native(elf);m.call('reusableInit');m.m.mem_write(m.s['SPI'],seed)
    result=m.call('identityBinary',inject);assert not result,('binary',inject,result)
    data=m.get('usbBytes',m.word('usbSize'));image,issues=decode_packets(data);assert not issues
    assert not decode_image(image)['issues']
    assert bytes(m.m.mem_read(m.s['SPI'],len(seed)))==seed
    exports.append(data)
assert exports[0]==exports[1]
print('PASS: pending/repeated DEVICE INFO cannot inject text into binary TPX3; old recordings/export bytes unchanged',flush=True)

elf=build('unavailable')
assert Native(elf).call('identityUnavailable')==0
print('PASS: unsupported targets report unavailable, never a fabricated device ID',flush=True)
