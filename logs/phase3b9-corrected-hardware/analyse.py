from pathlib import Path
import json,struct,zlib,hashlib,statistics,sys,re
ROOT=Path.cwd();OUT=ROOT/'logs/phase3b9-corrected-hardware';sys.path.insert(0,str(ROOT/'tools'))
from tcb3_decode import decode_packets,decode_image,json_document,page_valid
b=(OUT/'append-30s-1.tpx3').read_bytes();assert b==(OUT/'append-30s-2.tpx3').read_bytes()
image,issues=decode_packets(b);doc=decode_image(image);assert not issues and not doc['issues']
old,new=doc['sessions'];assert [s['session_id'] for s in doc['sessions']]==[1,3]
archive=(ROOT/'logs/phase3b7-hardware/first-write-1.tpx3').read_bytes();old_image,_=decode_packets(archive)
assert image[:8192]==old_image
assert old['raw']==(ROOT/'logs/phase3b7-hardware/first-write.v2wire').read_bytes()
assert new['footer']['committed']==[3000,300,30,0]
assert new['footer']['offered']==new['footer']['committed'] and new['footer']['lost']==[0]*4
assert all(new['footer'][k]==0 for k in ('reason','rejected','crc_failures','incomplete_writes','full','missed'))
(OUT/'recovered-recording-image.bin').write_bytes(image)
# Isolate actual PAGE payloads and reframe packet sequences/CRCs for independent exports.
packets=[];pos=0
while pos<len(b):
 n=struct.unpack_from('<H',b,pos+16)[0];p=b[pos:pos+n+24];packets.append(p);pos+=n+24
for s in (old,new):
 start=s['sectors'][0];end=s['sectors'][-1]+4096
 selected=[p for p in packets if p[5]!=2 or start<=struct.unpack_from('<I',p,12)[0]<end]
 framed=bytearray()
 for seq,p in enumerate(selected):
  h=bytearray(p[:-4]);struct.pack_into('<I',h,8,seq);framed+=h+struct.pack('<I',zlib.crc32(h))
 (OUT/f"session-{s['session_id']}.tpx3").write_bytes(framed)
 isolated,export_issues=decode_packets(bytes(framed));isolated_doc=decode_image(isolated)
 assert not export_issues and not isolated_doc['issues'] and len(isolated_doc['sessions'])==1
 assert isolated_doc['sessions'][0]['raw']==s['raw'] and isolated_doc['sessions'][0]['status']=='complete'
 (OUT/f"session-{s['session_id']}.v2wire").write_bytes(s['raw'])
 isolated_doc['sessions'][0].pop('raw')
 (OUT/f"session-{s['session_id']}.json").write_text(json.dumps(json_document(isolated_doc),indent=2)+'\n')
assert (OUT/'session-1.tpx3').read_bytes()==archive
analysis={'streams':{},'ordering':{},'pages':[],'allocated_sectors':new['sectors'],'old_export_reframed_byte_identical':True}
for kind in (1,2,3):
 rows=[r for r in new['records'] if r['type_id']==kind];ts=[r['timestamp_us'] for r in rows];steps=[c-a for a,c in zip(ts,ts[1:])]
 assert all(x>0 for x in steps)
 target=10000 if kind==1 else 100000 if kind==2 else 1000000
 summary={'count':len(rows),'first_us':ts[0],'last_us':ts[-1],'mean_hz':(len(ts)-1)*1e6/(ts[-1]-ts[0]),'spacing_min_us':min(steps),'spacing_max_us':max(steps),'spacing_mean_us':statistics.mean(steps),'spacing_stdev_us':statistics.pstdev(steps),'max_absolute_spacing_error_us':max(abs(x-target) for x in steps),'nonpositive_spacing':0}
 if kind>1:
  utc=[r['utc']['time_of_day_ms'] for r in rows if r.get('utc') and r['utc'].get('time_of_day_ms') is not None]
  summary.update(utc_epochs=len(utc),unique_utc_epochs=len(set(utc)),utc_step_ms=sorted(set((c-a)%86400000 for a,c in zip(utc,utc[1:]))),validity_masks=sorted(set(r['validity_mask'] for r in rows)))
  if kind==2:summary['valid_fix_count']=sum(r['fix_valid'] for r in rows)
  else:summary.update(fix_quality_values=sorted(set(r['fix_quality'] for r in rows)),satellites_used_values=sorted(set(r['satellites_used'] for r in rows)),hdop_values=sorted(set(r['hdop'] for r in rows)))
 analysis['streams'][str(kind)]=summary
records=new['records'];times=[r['timestamp_us'] for r in records]
assert all(c>=a for a,c in zip(times,times[1:]))
analysis['ordering']={'all_records_monotonic':True,'equal_timestamp_pairs':sum(c==a for a,c in zip(times,times[1:])),'event_records':sum(r['type_id']==4 for r in records)}
analysis['metadata_to_stop_us']=new['footer']['stop_us']-new['metadata']['start_us']
analysis['last_observation_to_stop_us']=new['footer']['stop_us']-times[-1]
analysis['raw_bytes']=len(new['raw']);analysis['frames']=new['frames'];analysis['footer']=new['footer'];analysis['metadata']=new['metadata']
for a in range(8192,len(image),256):
 p=image[a:a+256]
 if p==b'\xff'*256:continue
 page_valid(p)
 assert p[:4] in (b'TPH3',b'TPD3',b'TPE3') and struct.unpack_from('<I',p,4)[0]==3
 assert 8192<=a and a+256<=262144
 analysis['pages'].append({'address':a,'address_hex':f'0x{a:06X}','end_hex':f'0x{a+255:06X}','magic':p[:4].decode(),'page_sequence':struct.unpack_from('<I',p,8)[0],'sector_sequence':struct.unpack_from('<I',p,20)[0] if p[:4]==b'TPH3' else None})
result=json.loads((OUT/'record-result.json').read_text());final=result['final']['TCB STATUS']
programs=int(re.search(r'programs=(\d+)',final)[1]);assert programs==len(analysis['pages'])
analysis['programmed_page_count']=programs
analysis['max_committed_address_hex']=analysis['pages'][-1]['end_hex']
analysis['max_service_us']=int(re.search(r'max_service_us=(\d+)',final)[1]);analysis['max_block_ms']=int(re.search(r'max_block_ms=(\d+)',final)[1])
for s in doc['sessions']:del s['raw']
(OUT/'decoded.json').write_text(json.dumps(json_document(doc),indent=2)+'\n')
(OUT/'analysis.json').write_text(json.dumps(analysis,indent=2)+'\n')
print(json.dumps({k:v for k,v in analysis.items() if k not in ('pages','frames','metadata')},indent=2))
