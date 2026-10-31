"""Production embedded encoder on ARM, exact benchmark/V2 fixtures, fixed RAM.

No device access. Timing counts are instruction measurements, NOT physical WCET.
"""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.dont_write_bytecode=True
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'.pio-core/test-python')]
import storage_benchmark as bench
from compact_decode import decode_frame, decode_stream
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_BLOCK, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC

OUT=ROOT/'.pio/compact-tests.elf'
compiler=ROOT/'.pio-core/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-g++.exe'
subprocess.run([str(compiler),'-std=c++11','-mcpu=cortex-m4','-mthumb','-Ofast',
                '-fno-exceptions','-fno-rtti','-fno-unwind-tables','-ffunction-sections','-fdata-sections',
                '-fstack-usage','-nostartfiles','-I'+str(ROOT/'tests/flash_write'),
                str(Path(__file__).with_name('harness.cpp')),'-Wl,-Ttext=0x10000','-Wl,-Tdata=0x20000000',
                '-Wl,-e,prepare','-Wl,--gc-sections','-Wl,-u,runScenario','-Wl,-u,run','-Wl,-u,slice','-o',str(OUT)],check=True,cwd=OUT.parent)
with OUT.open('rb') as stream:
    elf=ELFFile(stream)
    symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    loads=[(s['p_vaddr'],s.data()) for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
    init=elf.get_section_by_name('.init_array')
    constructors=list(struct.unpack('<'+'I'*(init.data_size//4),init.data())) if init else []


def machine():
    m=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
    m.mem_map(0x10000,0x100000);m.mem_map(0x20000000,0x200000)
    for at,data in loads:m.mem_write(at,data)
    for at in constructors:call(m,at,0)
    return m


def call(m,name,arg=0):
    m.reg_write(UC_ARM_REG_SP,0x201FF000);m.reg_write(UC_ARM_REG_LR,0xF0001);m.reg_write(UC_ARM_REG_R0,arg)
    m.emu_start((symbols[name] if isinstance(name,str) else name)|1,0xF0000,count=100000000)
    if m.reg_read(UC_ARM_REG_PC)!=0xF0000:raise AssertionError('instruction bound')
    return m.reg_read(UC_ARM_REG_R0)


def encode(records,profile=False,cycles_per_instruction=0):
    m=machine();raw=bench.canonical(records)
    m.mem_write(symbols['compactTestInput'],raw)
    assert call(m,'prepare',len(raw))==0
    evidence={'instructions':0,'blocks':0,'stack_low':0x201FF000,'max_slice_instructions':0}
    cache={}
    def block(uc,address,size,_):
        if (address,size) not in cache:
            code=bytes(uc.mem_read(address,size));at=0;n=0
            while at<len(code):
                half=int.from_bytes(code[at:at+2],'little')
                at+=4 if half>>11 in (0b11101,0b11110,0b11111) else 2;n+=1
            cache[address,size]=n
        evidence['instructions']+=cache[address,size];evidence['blocks']+=1
        if cycles_per_instruction:
            uc.mem_write(symbols['compactTestNow'],struct.pack('<Q',evidence['instructions']*cycles_per_instruction//64))
    def stack(uc,access,address,size,value,_):evidence['stack_low']=min(evidence['stack_low'],address)
    if profile:
        hook=m.hook_add(UC_HOOK_BLOCK,block)
        stack_hook=m.hook_add(UC_HOOK_MEM_WRITE,stack,begin=0x201F0000,end=0x201FFFFF)
        calls=0
        while True:
            before=evidence['instructions'];ready=call(m,'slice',1);calls+=1
            evidence['max_slice_instructions']=max(evidence['max_slice_instructions'],evidence['instructions']-before)
            if ready:break
            assert calls<30000
        evidence['slices']=calls;evidence['stack_bytes']=0x201FF000-evidence['stack_low']
        m.hook_del(hook);m.hook_del(stack_hook)
    else:assert call(m,'run',0)==0
    size=struct.unpack('<I',bytes(m.mem_read(symbols['compactTestResult'],4)))[0]
    evidence['clock_max_slice_us'],evidence['clock_max_block_step_us'],evidence['max_units']=struct.unpack(
        '<3I',bytes(m.mem_read(symbols['compactTestResult']+8,12)))
    evidence['cycles_per_instruction_model']=cycles_per_instruction
    frame=bytes(m.mem_read(symbols['compactTestOutput'],size))
    decoded=decode_frame(frame)
    if decoded['raw']!=raw:
        at=next((i for i,(a,b) in enumerate(zip(decoded['raw'],raw)) if a!=b), min(len(decoded['raw']),len(raw)))
        raise AssertionError(('reconstruction',at,decoded['raw'][max(0,at-16):at+24].hex(),raw[max(0,at-16):at+24].hex()))
    return frame,evidence


def golden_records():
    for path in sorted((ROOT/'tests/recorder_v2/fixtures').glob('*.bin')):
        raw=path.read_bytes();records=[]
        for at in range(0,len(raw),4096):
            if raw[at:at+4]!=b'TPH2':continue
            for page in range(1,15):
                data=raw[at+page*256:at+(page+1)*256]
                if data[:4]!=b'TPD2' or int.from_bytes(data[252:],'little')!=0xC0DEC0DE:break
                import zlib
                if zlib.crc32(data[:248])!=int.from_bytes(data[248:252],'little'):break
                count,used=struct.unpack_from('<HH',data,12);index=16
                for _ in range(count):
                    kind,version,size,us=struct.unpack_from('<BBHQ',data,index)
                    values=struct.unpack(bench.FORMATS[kind],data[index+12:index+size])
                    records.append(bench.Record(kind,us,values));index+=size
        if records:yield path.stem,records


def main():
    results={'compiler':'ARM GCC -Ofast, Cortex-M4 Thumb','hardware_timing_validated':False,
             'timing_model':'instruction counts only; no MCU cycles or interrupts simulated', 'cases':{}}
    for scenario in range(10):
        assert call(machine(),'runScenario',scenario)==0,scenario
        print('PASS: native control/backpressure scenario',scenario,flush=True)
    for name,records in golden_records():
        frame,evidence=encode(records,True)
        results['cases']['golden_'+name]=dict(frame_bytes=len(frame),**evidence)
        print('PASS: native V2 golden',name,flush=True)
    # Existing benchmark sources, same seeds/provenance; all 435 two-second blocks.
    for name,blocks,source in bench.datasets(120,2):
        if name=='real_recording':
            # Real V1 timing is not exactly 100Hz. Reframe by measured time,
            # never invent or rescale timestamps to fit sample-count windows.
            rows=[r for block in blocks for r in block];blocks=[];block=[]
            for row in rows:
                if block and row.us-block[0].us>=2000000:
                    blocks.append(block);block=[]
                block.append(row)
            if block:blocks.append(block)
        sizes=[];worst=None;worst_evidence=None
        # Profile every source block, not only its first or largest byte size.
        profiles=[]
        for index,records in enumerate(blocks):
            try:frame,evidence=encode(records,profile=True)
            except Exception as exc:raise AssertionError((name,index)) from exc
            sizes.append(len(frame))
            profiles.append(evidence)
        pages=[(size+231)//232 for size in sizes]
        # Normalise real V1 data to the required 100Hz sample rate, and do
        # not count its final partial block as another full two seconds.
        weights=[sum(r.kind==1 for r in rows) for rows in blocks]
        cycles,spare=divmod(4095*14,sum(pages));imu_capacity=cycles*sum(weights)
        for page_count,weight in zip(pages,weights):
            if page_count>spare:break
            spare-=page_count;imu_capacity+=weight
        results['cases'][name]=dict(blocks=len(blocks),mean_bytes=sum(sizes)/len(sizes),max_bytes=max(sizes),
                                  pages_mean=sum(pages)/len(pages),duration_minutes_14pages=imu_capacity/6000,
                                  source=source,profiled_blocks=len(profiles),
                                  max_instructions=max(p['instructions'] for p in profiles),
                                  max_slice_instructions=max(p['max_slice_instructions'] for p in profiles),
                                  max_stack_bytes=max(p['stack_bytes'] for p in profiles),
                                  max_slices=max(p['slices'] for p in profiles))
        print('PASS: native benchmark',name,len(blocks),'blocks',flush=True)
    # Explicit fallback cases and hostile precision extremes.
    for kind,values in ((2,(127,507752560,-900030,1234,35999999,20261030,58460000,65,65,0x4e47)),
                        (3,(63,20261030,58460000,-1200,45000,128,7,1,0x4e47,0))):
        frame,evidence=encode([bench.Record(kind,1000000,values)],True)
        assert frame[5]==0
        results['cases']['fallback_'+str(kind)]=dict(frame_bytes=len(frame),**evidence)
    extremes=[bench.Record(4,(1<<64)-2000001+i*1000,
              (65535,3,4,0xffffffff if i%2 else 0,0xffffffff,(1<<64)-1-i)) for i in range(256)]
    frame,evidence=encode(extremes,True)
    results['cases']['full_8192_byte_events']=dict(frame_bytes=len(frame),**evidence)
    extremes=[bench.Record(3,i*10000,(63,20261030,i*10,-2147483648 if i%2 else 2147483647,
              2147483647 if i%2 else -2147483648,65535 if i%2 else 0,65535,254,0x4e47,0)) for i in range(200)]
    frame,evidence=encode(extremes,True)
    results['cases']['int32_extreme_quality']=dict(frame_bytes=len(frame),**evidence)
    real,_=bench.load_real(ROOT/'logs/device-monitor-261027-204904.log')
    frame,evidence=encode(real[3000:],True)
    results['cases']['real_partial_tail_26']=dict(frame_bytes=len(frame),**evidence)
    fixtures=ROOT/'tests/compact/fixtures';fixtures.mkdir(exist_ok=True)
    records=next(bench.datasets(2,2))[1][0]
    frame,_=encode(records)
    (fixtures/'stillness.tcb3').write_bytes(frame)
    (fixtures/'stillness.v2wire').write_bytes(bench.canonical(records))
    (fixtures/'manifest.json').write_text(json.dumps({'source':'existing storage_benchmark stillness seed, first 2s',
                 'frame_sha256':hashlib.sha256(frame).hexdigest(),'raw_sha256':hashlib.sha256(bench.canonical(records)).hexdigest()},indent=2)+'\n')
    for at in range(len(frame)):
        damaged=bytearray(frame);damaged[at]^=1
        try:decode_frame(damaged)
        except ValueError:pass
        else:raise AssertionError('corruption accepted')
    for at in range(len(frame)):
        try:decode_stream(frame[:at])
        except ValueError:pass
        else:assert at==0
    (ROOT/'.pio/phase3a-native-results.json').write_text(json.dumps(results,indent=2,sort_keys=True)+'\n')
    print('PASS: compact decoder corruption/truncation and byte-exact fixture',flush=True)


if __name__=='__main__':main()
