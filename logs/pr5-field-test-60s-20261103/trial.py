"""One USB physical-button trial: read-only serial allowlist, never START/STOP."""
from pathlib import Path
import sys,json,time,re,hashlib,importlib.util
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('restore',ROOT/'logs/pr5-field-candidate-20261103/restore_pr5.py');restore=importlib.util.module_from_spec(spec);spec.loader.exec_module(restore);hw=restore.hw;restore.OUT=OUT;hw.OUT=OUT
hw.Monitor.allowed=hw.Monitor.allowed|{'FIELD EVENTS'}
ALLOWED={'TCB STATUS','RECORD STATUS','OBS STATUS','COMP STATUS','GPS STATUS','BAT STATUS','DIAG ID','FIELD EVENTS'}
def save(n,v):hw.save(n,v)
def events(m):
 assert 'FIELD EVENTS' in ALLOWED
 m.log.write(b'\nHOST TX FIELD EVENTS\n');m.log.flush();m.link.write(b'FIELD EVENTS\n');m.link.flush();s=m.read(3)
 return dict(response=s,events=[dict(ms=int(a),event=b,session=int(c),detail=int(d),overwritten=int(e)) for a,b,c,d,e in re.findall(r'FIELD t_ms=(\d+) event=(\w+) session=(\d+) detail=(\d+) overwritten=(\d+)',s)])
def prepare():
 m=None;r=dict(passed=False,host_start_sent=False,host_stop_sent=False)
 try:
  r['candidate']=restore.verify();p=hw.identify();assert p.device=='COM8';r['device']=dict(port=p.device,vid=p.vid,pid=p.pid,serial=p.serial_number)
  m=hw.Monitor(OUT/'preflight-serial.txt');r['initial']=m.read(2);assert 'TPX3' not in r['initial'] and 'BEGIN_RECORD_CSV' not in r['initial']
  r['states']=m.idle();r['gps']=restore.gps(m);assert r['gps']['nmea_valid'],'GPS NMEA unavailable'
  r['flash']=restore.flash(m);r['battery']=m.command('BAT STATUS','BAT source=');assert 'usb_vbus=1' in r['battery'],'USB power not confirmed'
  r['archive']=restore.backup(m,'before-test');assert (OUT/'before-test-image.bin').read_bytes()==(ROOT/'logs/pr5-field-candidate-20261103/restore-20261103/after-upload-image.bin').read_bytes(),'Unexpected change from restoration inventory'
  r['baseline_events']=events(m);r['final_states']=m.idle();r['expected_new_session']=len((OUT/'before-test-image.bin').read_bytes())//4096+1;r['passed']=True
  print('PREFLIGHT PASS: COM8/USB, idle, GPS NMEA, flash/SFDP, 14-session backup preserved. Expected next session',r['expected_new_session'],flush=True)
 except Exception as exc:r['error']=str(exc);print('STOP:',exc,flush=True);raise
 finally:
  if m:m.close()
  r['serial_released']=True;save('preflight-result.json',r)
def live():
 pre=json.loads((OUT/'preflight-result.json').read_text());assert pre['passed'];expected=pre['expected_new_session']
 r=dict(status='starting_monitor',host_start_sent=False,host_stop_sent=False,polls=[],session=None);m=None
 try:
  m=hw.Monitor(OUT/'live-serial.txt');m.read(1);r['status']='waiting_for_physical_start';save('live-state.json',r);print('MONITOR READY: physical button actions only. Waiting for start.',flush=True)
  deadline=time.monotonic()+600;next_poll=0;buffer='';started=None;progress=0;stop_cue=False
  while time.monotonic()<deadline:
   now=time.monotonic()
   if now>=next_poll:
    m.log.write(b'\nHOST TX TCB STATUS\n');m.log.flush();m.link.write(b'TCB STATUS\n');m.link.flush();next_poll=now+1
   buffer+=m.read(.10)
   while '\n' in buffer:
    line,buffer=buffer.split('\n',1);line=line.strip()
    match=re.search(r'TCB (.*?) state=(\d+) session=(\d+)',line)
    if not match:continue
    label,state,sid=match[1],int(match[2]),int(match[3]);r['polls'].append(dict(host_monotonic=time.monotonic(),line=line,state=state,session=sid))
    if state==8 or 'lock=1' in line or ('error=' in line and 'error=none' not in line):raise RuntimeError('Recorder fault: '+line)
    for key in ('rejected','missed','full','crc','incomplete'):
     mm=re.search(r'\b'+key+r'=(\d+)',line)
     if started is not None and mm and int(mm[1]):raise RuntimeError('Unexpected recorder counter: '+line)
    if started is None and sid>0 and state not in (0,1,9):
     assert sid==expected,('Unexpected session',sid,expected)
     started=time.monotonic();r.update(session=sid,status='recording',capture_first_observed_monotonic=started,capture_first_observed_line=line)
     print('CAPTURE START DETECTED: session',sid,'; timing approximately 60 seconds from this point.',flush=True)
    if started is not None:
     if sid==0:raise RuntimeError('Unexpected restart/session loss')
     if sid!=expected:raise RuntimeError('Unexpected second session')
     if sid==expected and state==1:
      elapsed=time.monotonic()-started
      if elapsed<45:raise RuntimeError('Recording stopped earlier than expected: '+line)
      r.update(status='clean_idle_observed',elapsed_until_idle=elapsed,final_tcb=line)
      save('live-state.json',r);print('CLEAN IDLE DETECTED: session',sid,'finalised; monitoring stopped. No host STOP sent.',flush=True);return
   if started is not None:
    elapsed=time.monotonic()-started;r['elapsed_s']=elapsed
    if int(elapsed//10)>progress:progress=int(elapsed//10);print('RECORDING elapsed',round(elapsed,1),'seconds',flush=True)
    if elapsed>=54 and not stop_cue:
     stop_cue=True;r['status']='physical_stop_due';print('PHYSICAL STOP CUE: tap D0 briefly, release, then hold D0 for 3 seconds. Do not disconnect USB.',flush=True)
    if elapsed>95:raise RuntimeError('Expected physical stop not observed; host will not send STOP')
   save('live-state.json',r)
  raise RuntimeError('Physical-start/monitor timeout; no retry or host START/STOP')
 except Exception as exc:r.update(status='unexpected_stop',error=str(exc));print('UNEXPECTED: STOP AND REPORT:',exc,flush=True);raise
 finally:
  if m:m.close()
  r['serial_released']=True;save('live-state.json',r)
def collect():
 live=json.loads((OUT/'live-state.json').read_text());assert live['status']=='clean_idle_observed'
 r=dict(passed=False,host_start_sent=False,host_stop_sent=False);m=None
 try:
  m=hw.Monitor(OUT/'post-test-serial.txt');m.read(1);r['states']=m.idle();r['events']=events(m);r['gps']=restore.gps(m);r['flash']=restore.flash(m)
  r['archive']=restore.backup(m,'after-test');r['final_states']=m.idle();r['passed']=True
  print('POST-TEST EXPORT COMPLETE; recorder idle; serial closing.',flush=True)
 except Exception as exc:r['error']=str(exc);print('UNEXPECTED:',exc,flush=True);raise
 finally:
  if m:m.close()
  r['serial_released']=True;save('collection-result.json',r)
if __name__=='__main__':{'prepare':prepare,'live':live,'collect':collect}[sys.argv[1]]()
