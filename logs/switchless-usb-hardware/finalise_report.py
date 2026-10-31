"""Freeze final hardware evidence offline. No device access."""
from common import *
import hashlib,re,struct
pre=json.loads((OUT/'preflight.json').read_text())
up=json.loads((OUT/'upload-result.json').read_text())
post=json.loads((OUT/'postflight.json').read_text())
rec=json.loads((OUT/'recording-result.json').read_text())
ver=json.loads((OUT/'verification-result.json').read_text())
metrics=json.loads((OUT/'metrics.json').read_text())
assert pre['passed'] and up['uploaded'] and post['passed'] and rec['started'] and rec['finalised'] and ver['verified'] and ver['serial_released']
commands=[]
for path in OUT.glob('*-transcript.txt'):
    commands.extend(re.findall(r'^HOST TX (.*)$',path.read_text(),re.M))
assert commands.count('TCB START')==1 and commands.count('TCB DURATION 10')==1
assert not any('ERASE' in c or c.startswith('BAT MODE') or c.startswith('BAT CAL') or c.startswith('BAT LIMITS') for c in commands)
raw=bytearray(256)
for p,a,h in re.findall(r'FLASH ID SFDP pass=(\d+) address=([0-9A-F]+) ([0-9A-F]+)',post['identity']):
    if p=='01':raw[int(a,16):int(a,16)+16]=bytes.fromhex(h)
assert raw[:4]==b'SFDP'
ptr=int.from_bytes(raw[12:15],'little');density=struct.unpack_from('<I',raw,ptr+4)[0]
bits=(1<<(density&0x7fffffff)) if density&0x80000000 else density+1
assert bits//8==16777216
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
summary={'overall_usb_test':'PASS','firmware':'tacticpod_reusable_usb; TCB3 container 3.2.0',
    'approved_zip_sha256':sha(ROOT/'logs/switchless-battery-review/tacticpod_reusable_usb-switchless.zip'),
    'upload_exit_code':up['upload_exit_code'],'uploads':1,'recording_starts':1,
    'device':json.loads((OUT/'device.json').read_text()),'bootloader_port':up['bootloader_port'],
    'battery_absence':'User physically confirmed; USB remained connected; VBUS=1 before and after',
    'flash_bytes':bits//8,'sfdp_density_dword':hex(density),'flash_identity':'0B4018; SR1=00 SR2=02; fingerprint repeat PASS',
    'session_id':58,'metrics':metrics,'footer':ver['footer'],
    'host_observed_finalisation_s':rec['host_finalisation_s'],
    'previous_sessions_preserved':True,'previous_prefix_sha256':metrics['old_prefix_sha256'],
    'before_export_sha256':pre['export_sha256'],'after_export_sha256':ver['export_sha256'],
    'serial_released':True,'external_flash_erased':False,'bootloader_modified':False,
    'warning_notes':['One cumulative GPS checksum failure seen after boot; zero in fresh RATE window and no growth through capture',
        'Host initially asserted a nonexistent GPS field; read-only checks resumed without another upload',
        'Host initially checked RECORD STOPPED during expected read-only post-footer rescan; waited then exported without another recording',
        'Battery readings remain ADC_UNVALIDATED/unarmed; not a measurement of a connected cell',
        'Original GPS snapshot unavailable because profile already matched; automatic RAM-profile verification succeeded'],
    'battery_test_recommendation':'NO-GO pending resolution/review of startup electrical, charging/polarity and power-handover risks; USB-only software test does not qualify battery operation',
    'host_commands':commands}
save('RESULT.json',summary)
save('evidence-hashes.json',{p.name:sha(p) for p in OUT.iterdir() if p.is_file() and p.name!='evidence-hashes.json'})
print('RESULT PASS:',summary['session_id'],ver['footer']['committed'],'capacity',bits//8,'COM8 released')
