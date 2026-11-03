"""One approved PR5 application restoration; never records or runs GPS backup."""
from pathlib import Path
import sys,hashlib,json,zipfile,subprocess,importlib.util,time
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];CAND=Path(__file__).resolve().parent;OUT=CAND/'restore-20261103'
BIN='e8015cd7f400a5fdfcad784d211d72482451468783c8c9160566bf5cb3f612d6';ZIP='b6d37fcabadd341e7f732a6bb2af46054387b9ea8a08ca8955befc52c05334a5';SHA='3d4693207d459b1565af2e8b3955cd897f5d519c'
spec=importlib.util.spec_from_file_location('hw',ROOT/'.p6/logs/hardware-gps-backup-2026-11-03/hardware_run.py');hw=importlib.util.module_from_spec(spec);argv=sys.argv[:];sys.argv=[sys.argv[0]];spec.loader.exec_module(hw);sys.argv=argv;hw.OUT=OUT
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def save(n,v):hw.save(n,v)
def verify():
 m=json.loads((CAND/'manifest.json').read_text());w=Path(m['worktree'])
 assert m['commit']==SHA and m['environment']=='tacticpod_reusable_usb' and m['software_validation_passed'] and m['regressions']['passed']==31
 assert m['experimental_gps_backup_absent'] and not m['automatic_gps_backup']
 assert subprocess.check_output(['git','-C',str(w),'rev-parse','HEAD'],text=True).strip()==SHA
 for a in m['project_input_hashes']+m['framework_toolchain_references']:assert h(w/a['path'])==a['sha256'],a['path']
 for a in m['artifacts']:
  p=CAND/a['path'];assert p.stat().st_size==a['bytes'] and h(p)==a['sha256'],a['path']
 b=CAND/'tacticpod_reusable_usb/firmware.bin';zpath=CAND/'tacticpod_reusable_usb/firmware.zip'
 assert b.stat().st_size==202192 and h(b)==BIN and zpath.stat().st_size==203012 and h(zpath)==ZIP
 with zipfile.ZipFile(zpath) as z:
  package=json.loads(z.read('manifest.json'))['manifest'];assert set(package)=={'application','dfu_version'}
  app=package['application'];assert app['init_packet_data']['softdevice_req']==[0x123];assert z.read(app['bin_file'])==b.read_bytes()
 result=dict(commit=SHA,environment='tacticpod_reusable_usb',binary_bytes=202192,binary_sha256=BIN,zip_bytes=203012,zip_sha256=ZIP,manifest_sha256=h(CAND/'manifest.json'),package=str(zpath),source_verified=True,application_only=True,external_spi_erase_intended=False,experimental_backup=False)
 save('candidate-verification.json',result);return result

def gps(m):
 try:
  s=m.command('GPS STATUS','GPS speed_',8)
  return dict(response=s,nmea_valid='38400 | NMEA=VALID' in s,profile_verified='GPS configuration: CURRENT RAM readback verified: 100ms / RMC 10Hz / GGA 1Hz' in s)
 except Exception as exc:return dict(response=str(exc),nmea_valid=False,profile_verified=False,error='GPS status unavailable/refused; no recovery attempted')
def flash(m):
 s=m.command('DIAG ID','END_FLASH_ID',20)
 return dict(response=s,validation=hw.validate_flash_identity(s,(ROOT/'.p6/logs/phase3b4-hardware/sfdp-capture-1.bin').read_bytes()))
def backup(m,label):
 print('Capturing read-only recording backup:',label,flush=True)
 m.log.write(('\nHOST TX TCB READ [binary saved separately: '+label+'.tpx3]\n').encode());m.log.flush()
 a=m.backup(label);save(label+'-integrity.json',a)
 assert a['existing_decode_issues']==[dict(address=552960,error='interrupted final block')],'Unexpected recording integrity issue'
 print('Backup verified:',a['image_bytes'],'image bytes;',len(a['sessions']),'sessions; known interrupted session133 retained.',flush=True)
 return a

def prepare():
 OUT.mkdir(exist_ok=False);m=None;r=dict(passed=False,uploaded=False,recording_started=False)
 try:
  r['candidate']=verify();p=hw.identify();assert p.device=='COM8'
  r['device']=dict(port=p.device,vid=p.vid,pid=p.pid,serial=p.serial_number,hwid=p.hwid)
  m=hw.Monitor(OUT/'pre-upload-serial.txt');r['initial']=m.read(3)
  assert 'TPX3' not in r['initial'] and 'BEGIN_RECORD_CSV' not in r['initial'],'Export already active'
  r['states']=m.idle();r['flash']=flash(m)
  r['backup']=backup(m,'before-upload');r['backup_confirmation']=backup(m,'before-upload-confirmation')
  first=(OUT/'before-upload-image.bin').read_bytes();assert first==(OUT/'before-upload-confirmation-image.bin').read_bytes(),'Backup repeat mismatch'
  prior=ROOT/'.p6/logs/hardware-gps-backup-2026-11-03/hardware-run-approved-20261103/before-upload-image.bin'
  assert h(prior)=='d1c398b03deee2e8ea88a86779e856154ab0dfbac4a1165df1f62faf678e0379'
  assert first==prior.read_bytes(),'Recordings differ unexpectedly from preserved pre-experiment image; stop for review'
  r['recordings_match_pre_experiment_backup']=True;r['gps']=gps(m);r['final_states']=m.idle();r['passed']=True
  print('PREFLIGHT PASS: exact candidate and COM8; flash/SFDP; two matching full backups; recorder idle. GPS valid=',r['gps']['nmea_valid'],'profile verified=',r['gps']['profile_verified'],'(GPS-only failure does not block this approved rollback).',flush=True)
 except Exception as exc:r['error']=str(exc);print('STOP:',exc,flush=True);raise
 finally:
  if m:m.close()
  r['serial_released']=True;save('preflight-result.json',r)

def upload():
 pre=json.loads((OUT/'preflight-result.json').read_text());assert pre['passed'] and pre['serial_released']
 c=verify();assert (OUT/'before-upload-image.bin').read_bytes()==(OUT/'before-upload-confirmation-image.bin').read_bytes()
 p=hw.identify();assert p.device=='COM8' and p.serial_number==pre['device']['serial']
 r=dict(uploaded=False,application_only=True,external_spi_erase_intended=False,approved_binary_sha256=BIN,approved_zip_sha256=ZIP)
 with (OUT/'upload-attempted.json').open('x') as f:json.dump(r,f,indent=2)
 try:
  with hw.serial.Serial(p.device,1200,timeout=.1) as touch:touch.dtr=False
  loader=hw.identify(0x0045,25);assert loader.serial_number==hw.NUMBER
  r['loader']=dict(port=loader.device,vid=loader.vid,pid=loader.pid,serial=loader.serial_number,hwid=loader.hwid)
  command=[sys.executable,str(ROOT/'.p5/.pio-core/packages/tool-adafruit-nrfutil/adafruit-nrfutil.py'),'dfu','serial','-pkg',c['package'],'-p',loader.device,'-b','115200','--singlebank'];r['command']=command
  with (OUT/'dfu-invoked-once.json').open('x') as f:json.dump(r,f,indent=2)
  print('Uploading approved PR5 application exactly once through',loader.device,flush=True)
  with (OUT/'upload.txt').open('x') as log:proc=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=120)
  r['exit_code']=proc.returncode;assert proc.returncode==0,'DFU failed; no retry'
  r['uploaded']=True;app=hw.identify(0x8044,30);assert app.device=='COM8';r['application_port']=app.device
  print('UPLOAD PASS: application returned on COM8.',flush=True)
 finally:save('upload-result.json',r)

def post():
 up=json.loads((OUT/'upload-result.json').read_text());assert up['uploaded'] and up['exit_code']==0
 pre=json.loads((OUT/'preflight-result.json').read_text());r=dict(passed=False,uploaded=True,recording_test_run=False,recording_started=False);m=None
 try:
  m=hw.Monitor(OUT/'post-upload-serial.txt');r['startup']=m.read(10)
  assert 'TCB REUSABLE USB PROTOTYPE' in r['startup'] and 'GPS BACKUP TEST' not in r['startup'],'Unexpected startup/menu identity'
  r['states']=m.idle();r['gps']=gps(m);save('post-upload-result.json',r)
  if pre['gps']['nmea_valid'] and not r['gps']['nmea_valid']:raise RuntimeError('GPS communication unexpectedly lost after upload; stop')
  r['flash']=flash(m);r['recordings']=backup(m,'after-upload')
  assert (OUT/'after-upload-image.bin').read_bytes()==(OUT/'before-upload-image.bin').read_bytes(),'Recording contents changed after upload'
  assert r['recordings']['sessions']==pre['backup']['sessions'] and r['recordings']['existing_decode_issues']==pre['backup']['existing_decode_issues']
  r['recording_contents_and_integrity_preserved']=True;r['final_states']=m.idle()
  r['software_ready_for_separate_usb_recording_test']=r['gps']['nmea_valid'] and r['gps']['profile_verified']
  r['physical_led_observation']='pending owner confirmation; no supported serial LED/GPIO readback'
  r['passed']=True
  print('POST CHECKS PASS: startup/idle, flash and complete recording image/integrity preserved. GPS valid=',r['gps']['nmea_valid'],'10Hz profile=',r['gps']['profile_verified'],'. No recording test.',flush=True)
 except Exception as exc:r['error']=str(exc);print('STOP:',exc,flush=True);raise
 finally:
  if m:m.close()
  r['serial_released']=True;save('post-upload-result.json',r)
if __name__=='__main__':{'prepare':prepare,'upload':upload,'post':post}[sys.argv[1]]()
