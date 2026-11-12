"""Link-map RAM and measured ARM stack usage; never opens physical devices."""
import json
import os
from pathlib import Path
import sys
sys.dont_write_bytecode=True
import run_tests as native
from unicorn import UC_HOOK_MEM_WRITE

firmware=Path(os.environ.get('PLATFORMIO_BUILD_DIR', native.ROOT/'.pio/build'))/'seeed_xiao_nrf52840_sense/firmware.elf'
with firmware.open('rb') as stream:
    elf=native.ELFFile(stream)
    static=sum(elf.get_section_by_name(k)['sh_size'] for k in ('.data','.bss'))
    objects={s.name:s['st_size'] for s in elf.get_section_by_name('.symtab').iter_symbols()
             if 'CompactDetail' in s.name and s['st_info']['type']=='STT_OBJECT' and
             s['st_value']>=0x20000000}
# Phase 3B.4 adds a fixed 552-byte read-only identity snapshot/state. Keep a
# normal-build budget while checking compact pipeline objects independently.
print('RESOURCE: production static RAM',static,'limit',119000,
      '; compact objects',sum(objects.values()),'limit',38000,flush=True)
assert static<=119000, f'production static RAM {static} exceeds 119000 bytes'
assert sum(objects.values())<=38000, f'compact objects {sum(objects.values())} exceed 38000 bytes'
command=next(k for k in native.symbols if 'handleCommand' in k and 'CompactRecorder' in k)
m=native.machine();low=[0x201FF000]
def stack(uc,access,address,size,value,_):low[0]=min(low[0],address)
hook=m.hook_add(UC_HOOK_MEM_WRITE,stack,begin=0x201F0000,end=0x201FFFFF)
for text in (b'COMP START\0',b'COMP STATUS\0',b'COMP STOP\0',b'COMP CLEAR\0'):
    m.mem_write(native.symbols['compactTestInput'],text)
    assert native.call(m,command,native.symbols['compactTestInput'])==1
m.hook_del(hook)
stack_bytes=0x201FF000-low[0]
assert stack_bytes<=1024, f'command stack {stack_bytes} exceeds 1024 bytes'
profiles=json.loads((native.ROOT/'.pio/phase3a-native-results.json').read_text())['cases'].values()
encoder_stack=max(v.get('max_stack_bytes',v.get('stack_bytes',0)) for v in profiles)
assert encoder_stack<=1024, f'encoder stack {encoder_stack} exceeds 1024 bytes'
result=dict(firmware_static_bytes=static,phase2_static_bytes=80456,
            static_increase_bytes=static-80456,compact_objects=objects,
            compact_objects_bytes=sum(objects.values()),measured_command_stack_bytes=stack_bytes,
            measured_encoder_stack_bytes=encoder_stack,encoder_only_observed_static_plus_stack=static+encoder_stack,
            command_only_observed_static_plus_stack=static+stack_bytes,
            whole_firmware_peak_validated=False,
            note='ARM harness stack only; excludes live interrupts, framework/runtime stack/heap peaks')
(native.ROOT/'.pio/phase3a-resources.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS: production static RAM',static,'bytes; compact objects',sum(objects.values()),
      'bytes; observed command stack',stack_bytes,'bytes')
