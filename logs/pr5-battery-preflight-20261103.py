"""Approved read-only battery preflight. No recording, settings, recovery or upload."""
from pathlib import Path
import importlib.util,sys,json,time
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'logs/pr5-battery-preflight-20261103';OUT.mkdir(exist_ok=False)
spec=importlib.util.spec_from_file_location('restore',ROOT/'logs/pr5-field-candidate-20261103/restore_pr5.py')
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r);r.OUT=OUT;r.hw.OUT=OUT
r.hw.Monitor.allowed={'RECORD STATUS','OBS STATUS','COMP STATUS','TCB STATUS','GPS STATUS','BAT STATUS','DIAG ID'}
result=dict(passed=False,device_commands_read_only=True,settings_changed=False,firmware_identity_method='Verified archived package/source and recorded successful upload provenance; no live firmware hash command exists in this protocol')
m=None
try:
 result['candidate']=r.verify()
 port=r.hw.identify();assert port.device=='COM8'
 result['device']=dict(port=port.device,vid=port.vid,pid=port.pid,serial=port.serial_number,hwid=port.hwid)
 m=r.hw.Monitor(OUT/'serial.txt')
 print('COM8 confirmed. Passively waiting for GPS profile completion; no commands sent.',flush=True)
 passive=m.read(12)
 if 'GPS PROFILE END' not in passive:
  print('No profile end seen yet; extending passive wait to cover the 60-second job deadline.',flush=True)
  passive+=m.read(53)
 result['passive_observation']=passive
 assert 'TPX3' not in passive and 'BEGIN_RECORD_CSV' not in passive,'Export traffic observed'
 result['idle_before']=m.idle()
 gps=m.command('GPS STATUS','GPS speed_',10)
 result['gps']=gps
 assert '38400 | NMEA=VALID' in gps,'GPS NMEA readiness failed'
 assert 'GPS configuration: CURRENT RAM readback verified: 100ms / RMC 10Hz / GGA 1Hz' in gps,'Fresh GPS 10 Hz profile verification unavailable'
 result['flash']=r.flash(m)
 result['battery']=m.command('BAT STATUS','BAT source=',10)
 result['idle_after']=m.idle()
 result['passed']=True
 print('Read-only preflight complete. GPS, flash and recorder checks passed. '+result['battery'].strip(),flush=True)
except Exception as exc:
 result['error']=str(exc);print('STOP: '+str(exc),flush=True)
finally:
 if m:m.close()
 result['serial_closed']=True
 (OUT/'result.json').write_text(json.dumps(result,indent=2)+'\n')
