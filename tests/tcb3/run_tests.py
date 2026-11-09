"""Actual ARM storage state machine and host simulated 16MiB NOR. No devices."""
import sys
sys.dont_write_bytecode=True
from native import ROOT,Native,compile
import struct,json,zlib,unittest,random
import storage_benchmark as codec
from tcb3_decode import decode_image,decode_packets
from compact_decode import decode_frame

ELF=compile('tests/tcb3/harness.cpp','tcb3-store-tests')
END=0xfff000
def frame(records,sequence):
    raw=codec.canonical(records);base=records[0].us;counts=[sum(r.kind==k for r in records) for k in range(1,5)]
    body=codec.transform(records,8,base);predictor=8
    if len(body)>len(raw):body=raw;predictor=0
    data=codec.HEADER.pack(b'TCB3',0,predictor,0,0,sequence,len(raw),len(body),*counts,base)+body
    return data+struct.pack('<I',zlib.crc32(data))

def machine():
    m=Native(ELF);m.call('init',1);m.call('tick',4095);assert m.word('info')==1;return m
def info(m):return struct.unpack('<10I',m.get('info',40))
def until(m,predicate,limit=20000):
    for _ in range(limit//10):
        m.call('tick',10)
        if predicate(info(m)):return info(m)
        assert m.word('info')!=8,('unexpected engine fault',info(m))
    raise AssertionError('progress bound')
def image(m,length=None):
    length=length or max(4096,info(m)[1]);return m.get('nor',length)
def save(m,frames,records):
    assert m.call('start')==1;until(m,lambda s:s[0]==4)
    for i,data in enumerate(frames):
        m.put('input',data);assert m.call('offer',len(data))==1
        until(m,lambda s:s[3]==i+1)
    rows=[r for block in records for r in block]
    m.put('offeredCounts',struct.pack('<4Q',*[sum(r.kind==k for r in rows) for k in range(1,5)]))
    delta=max(0,rows[-1].us//1000+1-m.word('nowMs'));m.call('tick',delta)
    m.call('stop');until(m,lambda s:s[8]==1)
    assert not m.word('violations')

class Persistent(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.blocks=next(codec.datasets(6,2))[1]
        cls.frames=[frame(rows,i) for i,rows in enumerate(cls.blocks)]
        cls.raw=codec.canonical([r for rows in cls.blocks for r in rows])
    def test_nor_model_bits_pages_and_sector_erase(self):
        m=machine();m.m.mem_write(m.s['info']+80,struct.pack('<I',64));m.put('input',b'\0'*64)
        self.assertEqual(m.call('programModel',0),1);m.call('tick',10);m.put('input',b'\xff'*64)
        self.assertEqual(m.call('programModel',0),0) # No zero-to-one programming.
        self.assertEqual(m.call('programModel',250),0) # Cannot cross page.
        self.assertEqual(m.call('eraseModel',1),0)
        self.assertEqual(m.call('eraseModel',END),0)
        self.assertEqual(m.call('eraseModel',0),1);m.call('tick',30)
        self.assertEqual(m.call('programModel',0),1)
    def test_roundtrip_continuations_and_export(self):
        m=machine();save(m,self.frames,self.blocks);data=image(m)
        decoded=decode_image(data);self.assertFalse(decoded['issues'])
        expected_metadata=struct.pack('<IIQQ12I',0x30200,0,1000,0,1000000,100,104,244,35000,10,1,38400,2000000,8228,1,0)+b'\xff'*88
        self.assertEqual(data[16:176],expected_metadata)
        self.assertEqual(decoded['sessions'][0]['raw'],self.raw)
        self.assertEqual(decoded['sessions'][0]['status'],'complete')
        session=decoded['sessions'][0]
        self.assertEqual(session['metadata']['accel_ug_per_lsb'],244)
        self.assertEqual(session['metadata']['gyro_udps_per_lsb'],35000)
        sys.path.insert(0,str(ROOT/'tools'))
        import tacticpod_decode
        csv=tacticpod_decode.imu_csv(decoded).splitlines()
        first=next(r for r in session['records'] if r['type']=='IMU_RAW')
        self.assertEqual(csv[1].split(',')[2:],
                         [f'{raw*0.000244:.6f}' for raw in first['axes_raw'][:3]]+
                         [f'{raw*0.035:.6f}' for raw in first['axes_raw'][3:]])
        legacy_packets,legacy_issues=decode_packets((ROOT/'tests/tcb3/fixtures/v32/example.tpx3').read_bytes())
        self.assertFalse(legacy_issues)
        legacy_fixture=decode_image(legacy_packets)
        self.assertEqual(legacy_fixture['sessions'][0]['metadata']['accel_ug_per_lsb'],61)
        self.assertEqual(legacy_fixture['sessions'][0]['metadata']['gyro_udps_per_lsb'],8750)
        self.assertEqual(legacy_fixture['sessions'][0]['raw'],
                         (ROOT/'tests/tcb3/fixtures/v32/expected.v2wire').read_bytes())
        packets=bytearray();n=m.call('exporting',1)
        while n:
            self.assertLessEqual(n,280);packets.extend(m.get('exportPacket',n));n=m.call('exporting')
        exported,issues=decode_packets(bytes(packets));self.assertFalse(issues)
        self.assertEqual(decode_image(exported)['sessions'][0]['raw'],self.raw)
    def test_json_u64_precision(self):
        from tcb3_decode import json_document
        result=json_document(dict(start_us=1<<63,device_id=(1<<64)-1,offered=[1<<63,0,0,0]))
        self.assertEqual(result['start_us'],str(1<<63));self.assertEqual(result['device_id'],str((1<<64)-1))
        self.assertEqual(result['offered'][0],str(1<<63))
    def test_mixed_v1_v2_preserved_and_legacy_decode(self):
        m=machine();fixture=(ROOT/'tests/recorder_v2/fixtures/mixed_v1_v2.bin').read_bytes()
        m.put('nor',fixture);m.call('init',0);m.call('tick',4095)
        save(m,self.frames,self.blocks)
        data=image(m);self.assertEqual(data[:len(fixture)],fixture)
        result=decode_image(data);self.assertEqual(result['sessions'][0]['raw'],self.raw)
        self.assertGreaterEqual(len(result['legacy']['sessions']),2)
    def test_no_boot_writes_and_reset_append(self):
        m=machine();self.assertEqual(m.word('programs'),0);save(m,self.frames,self.blocks)
        before=image(m);m.call('init',0);m.call('tick',4095)
        self.assertEqual(m.word('programs'),0);self.assertEqual(info(m)[1],len(before))
        save(m,self.frames,self.blocks);self.assertEqual(image(m)[:len(before)],before)
        sessions=decode_image(image(m))['sessions'];self.assertEqual(len(sessions),2)
        self.assertTrue(all(s['raw']==self.raw for s in sessions))
    def test_cancelled_scan_cannot_allocate_from_partial_scan(self):
        m=machine();m.call('init',0);m.call('tick',10);self.assertEqual(m.call('cancel'),1)
        self.assertEqual(m.call('start'),0);self.assertEqual(m.word('programs'),0)
    def test_diagnostic_canary_never_touched(self):
        m=machine();canary=bytes(range(256))*16;m.m.mem_write(m.s['nor']+END,canary)
        save(m,self.frames,self.blocks)
        self.assertEqual(bytes(m.m.mem_read(m.s['nor']+END,4096)),canary)
        self.assertEqual(m.word('violations'),0)
        self.assertEqual(m.word('modelErases'),0)
    def test_hidden_payload_refuses_overwrite(self):
        m=machine();m.m.mem_write(m.s['nor']+256,b'\0');self.assertEqual(m.call('start'),1)
        m.call('tick',100);self.assertEqual(m.word('info'),8);self.assertEqual(m.word('programs'),0)
    def test_wrong_block_crc(self):
        m=machine();m.call('start');until(m,lambda s:s[0]==4)
        data=bytearray(self.frames[0]);data[-1]^=1;m.put('input',data);m.call('offer',len(data));m.call('tick',200)
        self.assertEqual(info(m)[0],8);self.assertEqual(info(m)[6],1)
    def test_timestamp_counter_rollover_exact(self):
        rows=[codec.Record(1,(1<<32)-10000+i*20000,(-32768,32767,-1,0,1,2)) for i in range(2)]
        m=machine();save(m,[frame(rows,0)],[rows])
        self.assertEqual(decode_image(image(m))['sessions'][0]['raw'],codec.canonical(rows))
    def test_invalid_fix_missing_utc_and_u64_event_detail(self):
        rows=[codec.Record(2,1000000,(0,0,0,0,0,0,0,86,0,0x4e47)),
              codec.Record(3,1000010,(0,0,0,0,0,0,0,255,0x4e47,0)),
              codec.Record(1,1000020,(-32768,32767,-1,0,1,2)),
              codec.Record(4,1000030,(5,2,2,2,1,(1<<64)-1))]
        m=machine();save(m,[frame(rows,0)],[rows]);s=decode_image(image(m))['sessions'][0]
        self.assertEqual(s['raw'],codec.canonical(rows));self.assertFalse(s['records'][0]['fix_valid'])
        self.assertEqual(s['records'][0]['utc'],dict(date=None,time_of_day_ms=None))
        self.assertEqual(s['records'][-1]['detail'],(1<<64)-1)
    def test_program_poll_millisecond_wrap(self):
        m=machine();m.word('nowMs',0xfffffff0);m.call('start');until(m,lambda s:s[0]==4)
        self.assertEqual(m.word('violations'),0)
    def test_maximum_raw_block_paced_throughput(self):
        m=machine();m.call('start');until(m,lambda s:s[0]==4)
        events=[codec.Record(4,1000000+i*1000,(1,0,1,i,i,(1<<64)-1-i)) for i in range(256)]
        elapsed=[]
        for sequence in range(4):
            rows=[codec.Record(r.kind,r.us+sequence*2000000,r.values) for r in events]
            raw=codec.canonical(rows);data=codec.HEADER.pack(b'TCB3',0,0,0,0,sequence,len(raw),len(raw),0,0,0,256,rows[0].us)+raw
            data+=struct.pack('<I',zlib.crc32(data));self.assertEqual(len(data),8228)
            m.put('input',data);self.assertEqual(m.call('offer',len(data)),1);start=m.word('nowMs')
            until(m,lambda s:s[3]==sequence+1);elapsed.append(m.word('nowMs')-start)
        self.assertLessEqual(max(elapsed),2000)
        (ROOT/'.pio/phase3b-max-block.json').write_text(json.dumps(dict(frame_bytes=8228,pages_per_block=39,simulated_ms=elapsed))+'\n')
    def test_crc_valid_fragment_and_footer_corruption(self):
        m=machine();save(m,self.frames,self.blocks);base=image(m)
        for address,offset,value in ((256,20,b'\1\0'),(256,26,b'\xff\xff'),(256,16,b'\xff'*4),
                                    (len(base)-256,128,b'\xff'*4)):
            damaged=bytearray(base);damaged[address+offset:address+offset+len(value)]=value
            page=damaged[address:address+256];struct.pack_into('<I',damaged,address+248,zlib.crc32(page[:248]))
            result=decode_image(bytes(damaged))
            self.assertTrue(result['issues'])
    def test_torn_header_reserves_sector_after_reboot(self):
        m=machine();m.m.mem_write(m.s['nor'],b'TPH');m.call('init',0);m.call('tick',4095)
        self.assertEqual(info(m)[1],4096);self.assertEqual(m.word('programs'),0)
        save(m,self.frames,self.blocks);self.assertEqual(image(m)[:3],b'TPH')
    def test_export_unknown_sector_is_explicit_issue(self):
        m=machine();m.m.mem_write(m.s['nor'],b'BAD!');packets=bytearray();n=m.call('exporting',1)
        while n:packets.extend(m.get('exportPacket',n));n=m.call('exporting')
        _,issues=decode_packets(packets);self.assertEqual(len(issues),1)
    def test_export_busy_fault_cannot_look_complete(self):
        m=machine();m.word('stuck',1);n=m.call('exporting',1);self.assertGreater(n,0)
        with self.assertRaises(ValueError):decode_packets(m.get('exportPacket',n))
    def test_denied_program(self):
        m=machine();m.word('deny',1);m.call('start');m.call('tick',200)
        self.assertEqual(info(m)[0],8);self.assertEqual(info(m)[7],1)
    def test_single_program_per_page_between_erases(self):
        m=machine();m.word('partialLimit',1);save(m,self.frames,self.blocks)
        counts=m.get('pageProgramCounts',65536)
        self.assertEqual(max(counts),1);self.assertEqual(sum(counts),m.word('programs'))
    def test_reset_at_each_wire_slice_boundary(self):
        for release in (0,1):
            for n in (0,64,128,192,256):
                m=machine();m.call('start')
                for _ in range(300):
                    m.call('tick',1)
                    if m.word('stagedOpen')&255 and m.word('stagedSize')==n:break
                else:self.fail('wire stage not reached')
                m.call('resetHeld',release);m.call('tick',4095)
                self.assertEqual(m.word('programs'),0) # Boot scan never programs.
                self.assertEqual(info(m)[1],4096 if release and n else 0)
                # A complete page may finish when CS rises, but a partial one
                # cannot masquerade as a valid committed session header.
                if release and n<256:self.assertEqual(decode_image(image(m))['sessions'],[])
    def test_readback_corruption_no_commit(self):
        m=machine();m.word('corrupt',1);m.call('start');m.call('tick',300)
        self.assertEqual(info(m)[0],8);self.assertEqual(info(m)[6],1)
        self.assertEqual(decode_image(image(m))['sessions'],[])
    def test_body_corruption_during_commit_is_detected(self):
        m=machine();m.word('corruptCommit',1);m.call('start');m.call('tick',300)
        self.assertEqual(info(m)[0],8);self.assertEqual(info(m)[6],1)
        self.assertEqual(decode_image(image(m))['sessions'],[])
    def test_empty_session_finalises(self):
        m=machine();m.call('start');m.put('offeredCounts',b'\0'*32);m.call('stop');until(m,lambda s:s[8]==1)
        s=decode_image(image(m))['sessions'][0];self.assertEqual(s['records'],[]);self.assertEqual(s['status'],'complete')
    def test_busy_timeout(self):
        m=machine();m.word('delayMs',2000);m.call('start');m.call('tick',2000)
        self.assertEqual(info(m)[0],8);self.assertEqual(info(m)[7],1)
    def test_boot_busy_timeout(self):
        m=machine();m.call('init',0);m.word('stuck',1);m.call('tick',6000)
        self.assertEqual(info(m)[0],8);self.assertEqual(m.word('programs'),0)
    def test_flash_full_stops_before_diagnostic(self):
        m=machine();m.m.mem_write(m.s['nor']+END-8192,b'TPH1');m.call('init',0);m.call('tick',4095)
        self.assertEqual(info(m)[1],END-4096);m.call('start');until(m,lambda s:s[0]==4)
        # Raw block deliberately spans >one sector.
        rows=[codec.Record(1,i*5000,tuple(((i*7919+a*8191)&65535)-32768 for a in range(6))) for i in range(256)]
        raw=codec.canonical(rows);body=codec.HEADER.pack(b'TCB3',0,0,0,0,0,len(raw),len(raw),256,0,0,0,0)+raw
        data=body+struct.pack('<I',zlib.crc32(body));m.put('input',data);m.call('offer',len(data))
        until(m,lambda s:s[5]==1);m.put('offeredCounts',struct.pack('<4Q',256,0,0,0));m.call('stop',1)
        until(m,lambda s:s[8]==1);self.assertEqual(info(m)[3],0)
        self.assertEqual(m.get('nor',0x1000000)[END:],b'\xff'*4096);self.assertFalse(m.word('violations'))
        decoded=decode_image(m.get('nor',END));self.assertEqual(decoded['sessions'][0]['footer']['lost'],[256,0,0,0])
    def test_corrupt_page_recovers_later_independent_block(self):
        m=machine();save(m,self.frames,self.blocks);data=bytearray(image(m))
        data[256+80]^=1
        result=decode_image(bytes(data));self.assertTrue(result['issues'])
        self.assertEqual(result['sessions'][0]['raw'],codec.canonical(self.blocks[1]+self.blocks[2]))
    def test_corrupt_continuation_header_recovers_later_blocks(self):
        extra=[[codec.Record(r.kind,r.us+6000000,r.values) for r in rows] for rows in self.blocks]
        blocks=self.blocks+extra;frames=[frame(rows,i) for i,rows in enumerate(blocks)]
        m=machine();save(m,frames,blocks);data=bytearray(image(m));data[4096+24]^=1
        result=decode_image(bytes(data));self.assertTrue(result['issues'])
        self.assertEqual(result['sessions'][0]['raw'],codec.canonical(self.blocks[0]+extra[1]+extra[2]))
    def test_transport_read_failure_is_not_successful_export(self):
        m=machine();m.word('denyRead',1);packets=bytearray();n=m.call('exporting',1)
        while n:packets.extend(m.get('exportPacket',n));n=m.call('exporting')
        with self.assertRaises(ValueError):decode_packets(packets)
        m.call('init',0);m.word('denyRead',1);m.call('tick',1);self.assertEqual(info(m)[0],8)
    @staticmethod
    def export_bytes(m,mode):
        packets=bytearray();n=m.call('exporting',mode)
        while n:packets.extend(m.get('exportPacket',n));n=m.call('exporting')
        return bytes(packets)
    def test_bulk_export_is_byte_identical_to_64_byte_baseline(self):
        m=machine();save(m,self.frames,self.blocks)
        self.assertEqual(m.word('pageReads'),0) # Store, scan, verify and program never use the export-only read.
        baseline=self.export_bytes(m,1);self.assertEqual(m.word('pageReads'),0)
        bulk=self.export_bytes(m,2)
        self.assertEqual(bulk,baseline);self.assertGreater(m.word('pageReads'),0)
        exported,issues=decode_packets(bulk);self.assertFalse(issues)
        self.assertEqual(decode_image(exported)['sessions'][0]['raw'],self.raw)
        self.assertEqual(m.word('violations'),0)
    def test_bulk_export_reads_one_page_per_page_and_never_programs(self):
        m=machine();save(m,self.frames,self.blocks);programs=m.word('programs')
        before=m.word('reads');self.export_bytes(m,1);legacy=m.word('reads')-before
        before=m.word('reads');self.export_bytes(m,2);bulk=m.word('reads')-before
        pages=m.word('pageReads');self.assertGreater(pages,0)
        self.assertEqual(legacy-bulk,4*pages) # Each page replaces exactly four 64-byte reads; sector probes unchanged.
        self.assertEqual(m.word('programs'),programs);self.assertEqual(m.word('violations'),0)
    def test_bulk_page_failure_is_reported_as_issue(self):
        m=machine();save(m,self.frames,self.blocks);m.word('denyPage',2)
        with self.assertRaises(ValueError):decode_packets(self.export_bytes(m,2))
    def test_bulk_export_corruption_is_caught_by_packet_and_page_crc(self):
        m=machine();save(m,self.frames,self.blocks);m.word('corruptPage',2)
        exported,issues=decode_packets(self.export_bytes(m,2))
        self.assertTrue(issues or decode_image(exported)['issues'])
    def test_power_cut_each_program_byte(self):
        m=machine();save(m,self.frames[:1],self.blocks[:1]);count=m.word('programs')
        addresses=struct.unpack('<'+'I'*count,m.get('traceAddress',4*count))
        lengths=struct.unpack('<'+'I'*count,m.get('traceLength',4*count));payload=m.get('traceBytes',256*count)
        model=bytearray(b'\xff'*len(image(m)));cuts=0
        for i,(address,n) in enumerate(zip(addresses,lengths)):
            data=payload[i*256:i*256+n]
            # Each prefix is a different possible interrupted NOR program.
            for k in range(n+1):
                torn=bytearray(model)
                for j in range(k):torn[address+j]&=data[j]
                decoded=decode_image(bytes(torn));recovered=b''.join(s['raw'] for s in decoded['sessions'])
                self.assertTrue(codec.canonical(self.blocks[0]).startswith(recovered));cuts+=1
            for j,v in enumerate(data):
                self.assertEqual(model[address+j]&v,v);model[address+j]&=v
        self.assertGreater(cuts,2000)
        (ROOT/'.pio/phase3b-powercuts.json').write_text(json.dumps(dict(byte_cut_positions=cuts,programs=count))+'\n')
    def test_nonsequential_torn_program_internal_order(self):
        m=machine();save(m,self.frames[:1],self.blocks[:1]);count=m.word('programs')
        addresses=struct.unpack('<'+'I'*count,m.get('traceAddress',4*count));payload=m.get('traceBytes',256*count)
        model=bytearray(b'\xff'*len(image(m)));rng=random.Random(303);cuts=0;raw=codec.canonical(self.blocks[0])
        for i,address in enumerate(addresses):
            p=payload[i*256:(i+1)*256]
            for n in range(257):
                torn=bytearray(model);torn[address+256-n:address+256]=p[256-n:]
                decoded=decode_image(bytes(torn));self.assertTrue(raw.startswith(b''.join(s['raw'] for s in decoded['sessions'])));cuts+=1
            for trial in range(32):
                torn=bytearray(model)
                # Simulate arbitrary cell order, including a complete seal before
                # data is fully programmed. CRC collisions remain a stated risk.
                for b in range(256):torn[address+b]&=p[b]|rng.randrange(256)
                torn[address+252:address+256]=p[252:]
                decoded=decode_image(bytes(torn));self.assertTrue(raw.startswith(b''.join(s['raw'] for s in decoded['sessions'])));cuts+=1
            model[address:address+256]=p
        (ROOT/'.pio/phase3b3-torn-order.json').write_text(json.dumps(dict(reverse_and_random_cases=cuts))+'\n')
    def test_torn_marker_native_lockout(self):
        m=machine();m.word('tearCall',1);m.word('tearBytes',252);m.call('start');m.call('tick',300)
        self.assertEqual(info(m)[0],8);self.assertEqual(info(m)[7],1)
        self.assertEqual(decode_image(image(m))['sessions'],[])
    def test_existing_v31_export_still_decodes(self):
        base=ROOT/'tests/tcb3/fixtures';old,issues=decode_packets((base/'example.tpx3').read_bytes())
        self.assertFalse(issues);decoded=decode_image(old)
        self.assertEqual(decoded['sessions'][0]['raw'],(base/'expected.v2wire').read_bytes())
        public=decode_image(old)
        for s in public['sessions']:del s['raw']
        from tcb3_decode import json_document
        self.assertEqual(json_document(public),json.loads((base/'expected.json').read_text()))
        m=machine();m.put('nor',old);m.call('init',0);m.call('tick',4095);save(m,self.frames,self.blocks)
        mixed=decode_image(image(m));self.assertEqual(mixed['container_version'],'mixed')
        self.assertEqual(len(mixed['sessions']),2);self.assertEqual(image(m)[:len(old)],old)
    def test_export_corruption_truncation_and_no_writes(self):
        m=machine();save(m,self.frames,self.blocks);before=m.word('programs')
        packets=bytearray();n=m.call('exporting',1)
        while n:packets.extend(m.get('exportPacket',n));n=m.call('exporting')
        self.assertEqual(m.word('programs'),before)
        for at in (0,1,19,23,len(packets)-1):
            with self.assertRaises(ValueError):decode_packets(packets[:at])
        for at in range(0,len(packets),97):
            bad=bytearray(packets);bad[at]^=1
            with self.assertRaises(ValueError):decode_packets(bad)

if __name__=='__main__':unittest.main(verbosity=2)
