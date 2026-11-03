"""Exactly one owner-approved application upload and one supervised cycle; no firmware edits."""
from pathlib import Path
import hashlib,importlib.util,json,re,subprocess,sys,time,zipfile
ROOT=Path(__file__).resolve().parents[2]
OUT=Path(__file__).with_name('hardware-run-force-resumed-one-20261103')
CAND=Path(__file__).with_name('force-backup-fix')
APPROVED='f6dcdf4ca6a3617153cecea7eff977327d8c932948e7912da1743c7103ba1434'
spec=importlib.util.spec_from_file_location('hw',Path(__file__).with_name('hardware_run.py'));hw=importlib.util.module_from_spec(spec)
argv=sys.argv[:];sys.argv=[sys.argv[0]];spec.loader.exec_module(hw);sys.argv=argv;hw.OUT=OUT
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def save(n,v):hw.save(n,v)
def verify():
 m=json.loads((CAND/'manifest.json').read_text());assert m['selected_environment']=='tacticpod_reusable_usb_gpsbackup'
 assert m['software_validation_passed'] and m['regressions']['passed']==32
 assert not m['automatic_backup_enabled_in_selected_environment'] and m['automatic_standby_define_in_selected_build']==0
 assert m['backup_request_hex']=='B5 62 02 41 10 00 00 00 00 00 00 00 00 00 06 00 00 00 08 00 00 00 61 6B'
 for a in m['artifacts']+m['changed_source_and_tests']+m['firmware_inputs']+m['framework_and_toolchain_references']:
  assert h(ROOT/a['path'])==a['sha256'],a['path']
 artifacts=[a for a in m['artifacts'] if a['environment']==m['selected_environment']]
 binary=ROOT/next(a['path'] for a in artifacts if a['path'].endswith('.bin'))
 package=ROOT/next(a['path'] for a in artifacts if a['path'].endswith('.zip'))
 elf=ROOT/next(a['path'] for a in artifacts if a['path'].endswith('.elf'))
 assert binary.stat().st_size==209536 and h(binary)==APPROVED
 with zipfile.ZipFile(package) as z:
  dfu=json.loads(z.read('manifest.json'))['manifest'];assert set(dfu)=={'application','dfu_version'}
  app=dfu['application'];assert app['init_packet_data']['softdevice_req']==[0x123]
  data=z.read(app['bin_file']);assert len(data)==209536 and hashlib.sha256(data).hexdigest()==APPROVED and data==binary.read_bytes()
 extracted=OUT/'verified-elf.bin'
 subprocess.run([str(ROOT/'.pio-core/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-objcopy.exe'),'-O','binary',str(elf),str(extracted)],check=True)
 assert h(extracted)==APPROVED
 result=dict(binary=str(binary),package=str(package),bytes=209536,binary_sha256=APPROVED,zip_sha256=h(package),manifest_sha256=h(CAND/'manifest.json'),source_and_artifacts_verified=True,elf_matches_application=True,application_only=True,external_spi_erase_intended=False,upload_method='Existing verified Seeed bootloader; Adafruit application-only serial DFU, singlebank. Bootloader writes internal MCU application flash; no external SPI erase command.',automatic_backup=False)
 save('candidate-verification.json',result);return result

def gps(m,initial=False):
 deadline=time.monotonic()+(60 if initial else 10)
 while time.monotonic()<deadline:
  m.log.write(b'\nHOST TX GPS STATUS\n');m.log.flush();m.link.write(b'GPS STATUS\n');m.link.flush();s=m.read(2)
  if 'GPS speed_' in s:
   assert '38400 | NMEA=VALID' in s and 'GPS configuration: CURRENT RAM readback verified: 100ms / RMC 10Hz / GGA 1Hz' in s,s
   return s
  if not initial or 'GPS profile operation active' not in s:raise RuntimeError('GPS status incomplete/refused: '+s)
 raise RuntimeError('GPS startup audit timeout')
def flash(m):
 s=m.command('DIAG ID','END_FLASH_ID',20)
 return dict(response=s,validation=hw.validate_flash_identity(s,(ROOT/'logs/phase3b4-hardware/sfdp-capture-1.bin').read_bytes()))
def prepare():
 OUT.mkdir(exist_ok=False);result=dict(passed=False);m=None
 try:
  result['candidate']=verify();p=hw.identify();assert p.device=='COM8'
  result['device']=dict(port=p.device,vid=p.vid,pid=p.pid,serial=p.serial_number,description=p.description,hwid=p.hwid,known_board='Previously verified XIAO nRF52840 Sense; unique USB serial matches prior approved upload')
  m=hw.Monitor(OUT/'pre-upload-serial.txt');result['initial']=m.read(3)
  assert 'TPX3' not in result['initial'] and 'BEGIN_RECORD_CSV' not in result['initial']
  result['states']=m.idle();result['gps']=gps(m);result['flash']=flash(m);result['final_states']=m.idle();result['gps_immediately_before_upload']=gps(m)
  result['passed']=True;print('PREFLIGHT PASS: exact approved BIN/manifest/ELF/ZIP; COM8 same unique XIAO; stopped/idle; NMEA+10Hz; five JEDEC IDs and both SFDP passes match. No external SPI erase intended.',flush=True)
 finally:
  if m:m.close()
  result['serial_released']=True;save('preflight-result.json',result)
def upload():
 pre=json.loads((OUT/'preflight-result.json').read_text());assert pre['passed'] and pre['serial_released']
 candidate=verify();p=hw.identify();assert p.device=='COM8' and p.serial_number==pre['device']['serial']
 result=dict(uploaded=False,approved_application_sha256=APPROVED,application_only=True,external_spi_erase_intended=False)
 with (OUT/'upload-attempted.json').open('x') as f:json.dump(result,f,indent=2)
 try:
  with hw.serial.Serial(p.device,1200,timeout=.1) as touch:touch.dtr=False
  loader=hw.identify(0x0045,25);assert loader.serial_number==hw.NUMBER
  result['loader']=dict(port=loader.device,vid=loader.vid,pid=loader.pid,serial=loader.serial_number,hwid=loader.hwid,identity_basis='Same unique serial as confirmed COM8 and previously verified Sense bootloader; expected 2886:0045')
  command=[sys.executable,str(ROOT/'.pio-core/packages/tool-adafruit-nrfutil/adafruit-nrfutil.py'),'dfu','serial','-pkg',candidate['package'],'-p',loader.device,'-b','115200','--singlebank']
  result['command']=command
  with (OUT/'dfu-invoked-once.json').open('x') as f:json.dump(result,f,indent=2)
  print('Uploading exact approved application once via',loader.device,flush=True)
  with (OUT/'upload.txt').open('x') as log:
   proc=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=120)
  result['exit_code']=proc.returncode;assert proc.returncode==0,'DFU failed; no retry'
  result['uploaded']=True;app=hw.identify(0x8044,30);assert app.device=='COM8';result['application_port']=app.device
  print('UPLOAD PASS: application returned on COM8.',flush=True)
 finally:save('upload-result.json',result)
def test():
 up=json.loads((OUT/'upload-result.json').read_text());assert up['uploaded'] and up['exit_code']==0
 result=dict(passed=False,cycle_count=0,uploaded=True,recording_started=False,external_flash_erased=False,automatic_gps_standby=False);m=None
 try:
  m=hw.Monitor(OUT/'post-upload-one-cycle-serial.txt');result['startup']=m.read(10)
  assert 'GPS BACKUP TEST - stopped-only' in result['startup'],'Command absent from startup menu'
  result['startup_states']=m.idle();result['startup_flash']=flash(m);result['startup_gps']=gps(m,True)
  save('test-result.json',result);print('STARTUP PASS: 115200, recording/capture idle, flash/SFDP normal, checksum-valid NMEA and current 10Hz RAM profile.',flush=True)
  with (OUT/'cycle-command-issued.json').open('x') as f:json.dump(dict(command='GPS BACKUP TEST',maximum_cycles=1,time_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())),f)
  result['cycle_count']=1;save('test-result.json',result)
  m.log.write(b'\nHOST TX GPS BACKUP TEST [exactly one approved cycle]\n');m.log.flush();m.link.write(b'GPS BACKUP TEST\n');m.link.flush()
  response='';deadline=time.monotonic()+100;last_progress=time.monotonic()
  while time.monotonic()<deadline:
   response+=m.read(.5)
   if 'GPS BACKUP TEST refused:' in response:raise RuntimeError('Test refused; no retry')
   if 'GPS BACKUP TEST END\n' in response.replace('\r',''):break
   if time.monotonic()-last_progress>=20:print('One supervised cycle still running; capturing serial, no extra commands.',flush=True);last_progress=time.monotonic()
  else:raise RuntimeError('Cycle/serial timeout; stop without recovery or extra commands')
  result['cycle_response']=response;(OUT/'cycle-1-response.txt').write_text(response,encoding='utf-8')
  save('test-result.json',result)
  required=['standby request queued (24 bytes, flags 0x06; no PMREQ ACK expected): YES','standby confirmed (UART silent 1500 ms after 3000 ms parked): YES (silence confirmed)','UART wake attempted: YES','GPS baud 38400: CONFIRMED (checksum-valid NMEA)','10 Hz configuration: VERIFIED','GPS BACKUP TEST: PASS','UART trace: host-read ms']
  missing=[s for s in required if s not in response]
  if missing:result['missing_success_evidence']=missing;raise RuntimeError('One-cycle result did not satisfy all backup/wake/recovery requirements; stop immediately')
  windows=re.findall(r'RX phase/bytes/complete-valid-NMEA/bad-checksum/first-ms/last-ms: (\d+)/(\d+)/(\d+)/(\d+)/(\d+)/(\d+)',response)
  parsed={int(row[0]):list(map(int,row[1:])) for row in windows};result['trace_windows']=parsed
  assert set(parsed)==set(range(1,8)) and parsed[3][0]==0 and parsed[4][1]>0,'Missing raw silence / valid wake NMEA evidence'
  result['post_cycle_gps']=gps(m);result['post_cycle_states']=m.idle();result['post_cycle_flash']=flash(m)
  result['recording_readiness']='PASS plus fresh NMEA and verified profile meets unchanged recordingReady predicate; no recording start probe or second cycle performed.'
  result['passed']=True;print('ONE CYCLE PASS: silence, wake, valid NMEA at38400, 10Hz profile, post-cycle flash/idle verified. No second cycle.',flush=True)
 except Exception as exc:
  result['error']=str(exc);print('STOPPED:',exc,flush=True);raise
 finally:
  if m:m.close()
  result['serial_released']=True;save('test-result.json',result)
if __name__=='__main__':{'prepare':prepare,'upload':upload,'test':test}[sys.argv[1]]()
