"""Offline verification only; no serial access."""
from pathlib import Path
import sys,json,re,hashlib,collections
sys.dont_write_bytecode=True
OUT=Path(__file__).resolve().parent;ROOT=OUT.parents[1]
sys.path.insert(0,str(ROOT/'.p5/tools'))
from tcb3_decode import decode_image,decode_packets,json_document
pre=json.loads((OUT/'preflight-result.json').read_text());post=json.loads((OUT/'collection-result.json').read_text())
images=[];decoded=[];transport=[]
for name in ('before-test','after-test'):
 image,issues=decode_packets((OUT/(name+'.tpx3')).read_bytes())
 assert image==(OUT/(name+'-image.bin')).read_bytes()
 assert not issues
 images.append(image);decoded.append(decode_image(image));transport.append(issues)
before,after=decoded
assert images[1][:len(images[0])]==images[0]
old={s['session_id']:s for s in before['sessions']};new={s['session_id']:s for s in after['sessions']}
assert all(new[k]==v for k,v in old.items())
added=set(new)-set(old);assert added=={162}
s=new[162];footer=s['footer'];records=s['records']
assert s['status']=='complete' and not s['issues'] and footer['reason']==0
assert before['issues']==after['issues']
assert footer['offered']==footer['committed']==[sum(r['type_id']==k for r in records) for k in range(1,5)]
assert not any(footer['lost'])
assert not any(footer[k] for k in ('rejected','crc_failures','incomplete_writes','full','missed'))
assert all(a['timestamp_us']<=b['timestamp_us'] for a,b in zip(records,records[1:]))
rates={}
for kind,name in ((1,'imu'),(2,'rmc'),(3,'gga')):
 rs=[r for r in records if r['type_id']==kind];ts=[r['timestamp_us'] for r in rs];gaps=[b-a for a,b in zip(ts,ts[1:])]
 assert gaps and min(gaps)>0
 rates[name]=dict(count=len(rs),observed_hz=(len(ts)-1)*1e6/(ts[-1]-ts[0]),min_gap_us=min(gaps),max_gap_us=max(gaps),first_us=ts[0],last_us=ts[-1])
 if kind==2:
  utc=[r['utc']['time_of_day_ms'] for r in rs]
  rates[name].update(valid_fixes=sum(r['fix_valid'] for r in rs),advancing_utc=all(b>a for a,b in zip(utc,utc[1:])),first_utc=rs[0]['utc'],last_utc=rs[-1]['utc'])
 if kind==3:rates[name]['valid_quality']=sum(r['fix_quality']>0 for r in rs)
assert 99<rates['imu']['observed_hz']<101 and 9.8<rates['rmc']['observed_hz']<10.2 and .9<rates['gga']['observed_hz']<1.1
assert rates['rmc']['advancing_utc']
text=(OUT/'post-test-serial.txt').read_text()
events=[dict(ms=int(a),event=b,session=int(c),detail=int(d),overwritten=int(e)) for a,b,c,d,e in re.findall(r'FIELD t_ms=(\d+) event=(\w+) session=(\d+) detail=(\d+) overwritten=(\d+)',text)]
counts=collections.Counter(e['event'] for e in events)
assert all(counts[k]==1 for k in ('start_accepted','session_created','stop_accepted','finalising','finalised_clean')) and counts['fault']==0
assert all(e['overwritten']==0 for e in events)
commands=[]
for name in ('preflight-serial.txt','live-serial.txt','post-test-serial.txt'):
 commands+=re.findall(r'^HOST TX (.+)$',(OUT/name).read_text(),re.M)
assert not any(re.search(r'\b(START|STOP|ERASE|RESET)\b',c.strip()) for c in commands)
result=dict(recording_data_passed=True,overall='RECORDING DATA PASS; post-test GPS STATUS refused during profile readback; no further hardware action',old_sessions_preserved=True,previous_ids=list(old),new_id=162,status=s['status'],capture_s=(footer['stop_us']-s['metadata']['start_us'])/1e6,start_us=s['metadata']['start_us'],footer=footer,rates=rates,new_session_issues=s['issues'],existing_container_issues=after['issues'],export_issues=transport,event_counts=dict(counts),events=events,host_commands=commands,physical_led_confirmation=dict(start='Owner confirmed solid green then green pulses about every 2 seconds',stop='Owner confirmed two green flashes then blue idle pulses'),post_test_gps_status=post['gps'],flash_passed=post['flash']['validation']['passed'],final_idle=post['final_states'],before_image_sha256=hashlib.sha256(images[0]).hexdigest(),after_image_sha256=hashlib.sha256(images[1]).hexdigest())
(OUT/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
(OUT/'session-162-wire.bin').write_bytes(s['raw'])
(OUT/'session-162.json').write_text(json.dumps(json_document({k:v for k,v in s.items() if k!='raw'}),indent=2)+'\n')
print(json.dumps({k:result[k] for k in ('overall','capture_s','rates','old_sessions_preserved','footer','event_counts')},indent=2))
