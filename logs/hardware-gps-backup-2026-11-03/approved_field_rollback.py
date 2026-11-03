"""Approved archived field rollback; no experimental recovery or recording test."""
from pathlib import Path
import importlib.util,sys,json,hashlib,zipfile,subprocess
ROOT=Path(__file__).resolve().parents[2];ARCH=ROOT.parent/'logs/field-retest-2026-11-02';OUT=Path(__file__).with_name('field-rollback-approved-ready-20261103')
BIN='40bced328dbcc453eb8326297fabab40c268063140d6c909a4359b4024a9a585';ZIP='c38561e5004ec88bf5d1b6b8b21c4d50a55cbbd19db41a7065c804438a6ae007';COMMIT='155a999122f0ce85ab6817c6eba690e9501fe4b3'
spec=importlib.util.spec_from_file_location('hw',Path(__file__).with_name('hardware_run.py'));hw=importlib.util.module_from_spec(spec);argv=sys.argv[:];sys.argv=[sys.argv[0]];spec.loader.exec_module(hw);sys.argv=argv;hw.OUT=OUT
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def save(n,v):hw.save(n,v)
def verify():
 binary=ARCH/'firmware.bin';package=ARCH/'firmware.zip'
 assert binary.stat().st_size==202320 and h(binary)==BIN and h(package)==ZIP
 with zipfile.ZipFile(package) as z:
  dfu=json.loads(z.read('manifest.json'))['manifest'];assert set(dfu)=={'application','dfu_version'}
  a=dfu['application'];assert a['init_packet_data']['softdevice_req']==[0x123]
  assert z.read(a['bin_file'])==binary.read_bytes()
 prior=json.loads((ARCH/'upload-result.json').read_text());assert prior['uploaded'] and prior['exit_code']==0 and prior['bin_sha256']==BIN and prior['zip_sha256']==ZIP
 source=subprocess.check_output(['git','show',COMMIT+':platformio.ini'],cwd=ROOT,text=True)
 assert '[env:tacticpod_reusable_usb]' in source and 'TACTICPOD_GPS_BACKUP' not in source
 receiver=subprocess.check_output(['git','show',COMMIT+':src/gps_receiver.cpp'],cwd=ROOT,text=True)
 assert 'GPS BACKUP TEST' not in receiver and 'buildBackupRequest' not in receiver
 result=dict(commit=COMMIT,environment='tacticpod_reusable_usb',bytes=binary.stat().st_size,bin_sha256=BIN,zip_sha256=ZIP,package=str(package),binary=str(binary),application_only=True,external_spi_erase_intended=False,experimental_gps_backup=False,verified_previous_upload=True)
 save('candidate-verification.json',result);return result

def gps(m):
 s=m.command('GPS STATUS','GPS speed_',8)
 save('latest-gps-status.json',dict(response=s))
 assert '38400 | NMEA=VALID' in s,'GPS unresponsive/unready: '+s
 assert 'GPS configuration: CURRENT RAM readback verified: 100ms / RMC 10Hz / GGA 1Hz' in s,'Current 10Hz profile unverified: '+s
 return s

def flash(m):
 s=m.command('DIAG ID','END_FLASH_ID',20)
 return dict(response=s,validation=hw.validate_flash_identity(s,(ROOT/'logs/phase3b4-hardware/sfdp-capture-1.bin').read_bytes()))
def prepare():
 OUT.mkdir(exist_ok=False);m=None;result=dict(passed=False,uploaded=False,recording_started=False)
 try:
  result['candidate']=verify();p=hw.identify();assert p.device=='COM8'
  result['device']=dict(port=p.device,vid=p.vid,pid=p.pid,serial=p.serial_number,hwid=p.hwid)
  m=hw.Monitor(OUT/'pre-upload-serial.txt');result['initial']=m.read(3)
  assert 'TPX3' not in result['initial'] and 'BEGIN_RECORD_CSV' not in result['initial'],'Export active'
  result['states']=m.idle();result['gps']=gps(m);result['flash']=flash(m)
  result['archive']=m.backup('before-upload');result['final_states']=m.idle();result['final_gps']=gps(m)
  result['passed']=True;print('PREFLIGHT PASS: archived field binary/package exact, COM8 identity, idle, GPS10Hz, flash/SFDP and full recording backup preserved.',flush=True)
 except Exception as exc:
  result['error']=str(exc);print('PREFLIGHT STOP:',exc,flush=True);raise
 finally:
  if m:m.close()
  result['serial_released']=True;save('preflight-result.json',result)
if __name__=='__main__':prepare()
