"""Whole-capacity paced producer against production ARM engine; no devices."""
import sys
sys.dont_write_bytecode=True
from native import ROOT,Native,compile
import struct,json,zlib
from tcb3_decode import page_valid
from compact_decode import decode_frame
elf=compile('tests/tcb3/harness.cpp','tcb3-sustained-tests')
results=[]
for mode in (0,1,3):
    m=Native(elf);m.call('init',1);m.call('tick',4095)
    # Protected sector canary; no new writer/exporter may access it.
    m.m.mem_write(m.s['nor']+0xfff000,b'\xa5'*4096)
    code=m.call('sustain',mode,limit=12000000000);metrics=struct.unpack('<16I',m.get('sustainedInfo',64))
    print('SUSTAIN',mode,'result',code,'metrics',metrics,flush=True)
    assert code==0 and metrics[7]==0 and metrics[8]==0xfff000 and metrics[9]==1
    assert metrics[2]==1 and metrics[1]==1470 and metrics[0]==1471
    assert metrics[3]<2000
    counts=m.get('pageProgramCounts',65536)
    assert max(counts)==1
    assert bytes(m.m.mem_read(m.s['nor']+0xfff000,4096))==b'\xa5'*4096
    # Streaming recovery checks all committed frames without materialising a
    # 376k-record JSON document. Validate every original timestamp/detail.
    pending=bytearray();expected=0
    for sector in range(4095):
        for page in range(1,15):
            p=bytes(m.m.mem_read(m.s['nor']+sector*4096+page*256,256));used=page_valid(p)
            seq=struct.unpack_from('<I',p,16)[0];offset,length,n,flags=struct.unpack_from('<4H',p,20)
            assert seq==expected and offset==len(pending) and length==8228 and used==16+n
            pending.extend(p[32:32+n])
            if flags&2:
                decoded=decode_frame(bytes(pending));assert decoded['sequence']==expected
                raw=decoded['raw'];base=struct.unpack_from('<Q',pending,24)[0]
                for i in range(256):
                    r=raw[i*32:(i+1)*32]
                    assert struct.unpack_from('<Q',r,4)[0]==base+i
                    assert struct.unpack_from('<IIQ',r,16)==(expected,i,(1<<64)-1-i-expected)
                expected+=1;pending.clear()
    assert expected==1470 and not pending
    results.append(dict(mode=mode,produced=metrics[0],committed=metrics[1],queue_high=metrics[2],
                        max_arrival_to_commit_ms=metrics[3],elapsed_ms=metrics[4],max_store_block_ms=metrics[5],
                        programs=metrics[6],compression_blackout_ms=metrics[10],program_busy_ms=metrics[11]))
(ROOT/'.pio/phase3b3-sustained.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS: paced maximum frames through entire recording capacity; no growing backlog')
