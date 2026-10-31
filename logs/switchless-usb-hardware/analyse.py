"""Offline analysis of the completed physical recording, no serial access."""
from common import *
import sys,hashlib,re,statistics
sys.path.insert(0,str(ROOT/'tools'))
from tcb3_decode import decode_packets,decode_image
r=json.loads((OUT/'verification-result.json').read_text());assert r['verified'] and r['serial_released']
data=(OUT/'after-1.tpx3').read_bytes();assert data==(OUT/'after-2.tpx3').read_bytes()
image,issues=decode_packets(data);assert not issues
doc=decode_image(image);s=doc['sessions'][-1]
streams={}
for kind,name in ((1,'IMU'),(2,'RMC'),(3,'GGA')):
    records=[p for p in s['records'] if p['type_id']==kind]
    times=[p['timestamp_us'] for p in records];gaps=[b-a for a,b in zip(times,times[1:])]
    m=dict(count=len(records),first_us=times[0],last_us=times[-1],unique_monotonic_timestamps=len(set(times)),
        min_spacing_us=min(gaps),max_spacing_us=max(gaps),mean_spacing_us=statistics.mean(gaps),
        observed_hz=(len(times)-1)*1000000/(times[-1]-times[0]))
    if kind!=1:
        utc=[p['utc']['time_of_day_ms'] for p in records]
        assert all(t is not None for t in utc) and len(set(utc))==len(utc) and all(b>a for a,b in zip(utc,utc[1:]))
        m.update(unique_utc_epochs=len(set(utc)),min_utc_spacing_ms=min(b-a for a,b in zip(utc,utc[1:])),
            max_utc_spacing_ms=max(b-a for a,b in zip(utc,utc[1:])),validity_masks=sorted(set(p['validity_mask'] for p in records)))
        if kind==2:m['valid_fixes']=sum(p['fix_valid'] for p in records)
        else:m['fix_quality_values']=sorted(set(p.get('fix_quality') for p in records))
    streams[name]=m
pages=[a for a in range(0x39000,len(image),256) if image[a:a+256]!=b'\xff'*256]
assert len(pages)==43 and pages[0]==0x39000 and pages[-1]==0x3bf00
assert image[:0x39000]==(OUT/'before-upload-image.bin').read_bytes()
gps_count=int(re.search(r'checksum_fail=(\d+)',r['gps'])[1]);assert gps_count==1
comp=r['states']['COMP STATUS'];tcb=r['states']['TCB STATUS']
metrics={'streams':streams,'capture_duration_us':s['footer']['stop_us']-s['metadata']['start_us'],
    'metadata_start_us':s['metadata']['start_us'],'footer_stop_us':s['footer']['stop_us'],
    'sectors':[hex(a) for a in s['sectors']],'programmed_pages':[hex(a) for a in pages],
    'actual_programmed_page_count':len(pages),'allocated_bytes':len(s['sectors'])*4096,
    'programmed_bytes':len(pages)*256,'next_append_address':hex(len(image)),
    'max_service_us':int(re.search(r'max_service_us=(\d+)',tcb)[1]),
    'max_block_ms':int(re.search(r'max_block_ms=(\d+)',tcb)[1]),
    'max_compression_slice_us':int(re.search(r'max_slice_us=(\d+)',comp)[1]),
    'max_compression_block_work_us':int(re.search(r'max_block_step_us=(\d+)',comp)[1]),
    'exports_sha256':hashlib.sha256(data).hexdigest(),'export_bytes':len(data),
    'old_prefix_sha256':hashlib.sha256(image[:0x39000]).hexdigest(),
    'cumulative_gps_checksum_failures_before_after':1,
    'previous_sessions_preserved':True,'serial_released':True,'recording_start_commands':1,'uploads':1}
save('metrics.json',metrics)
print(json.dumps(metrics,indent=2))
