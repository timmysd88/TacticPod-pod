"""Read-only source/binary provenance audit. No device access or bootloader build."""
from pathlib import Path
import json,re,hashlib
ROOT=Path(__file__).resolve().parents[2];OUT=Path(__file__).resolve().parent
BOOT=OUT/'adafruit-Adafruit_nRF52_Bootloader-459adc9'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def image(path):
    memory={};base=0
    for line in path.read_text().splitlines():
        b=bytes.fromhex(line[1:]);assert line.startswith(':') and sum(b)%256==0
        n=b[0];at=int.from_bytes(b[1:3],'big');kind=b[3];data=b[4:4+n]
        assert len(b)==n+5
        if kind==4:base=int.from_bytes(data,'big')<<16
        elif kind==2:base=int.from_bytes(data,'big')<<4
        elif kind==0:
            for i,value in enumerate(data):memory[base+at+i]=value
    # Separate sparse regions: no allocation across UICR/flash gaps.
    chunks=[];start=None;buf=bytearray();previous=None
    for at,value in sorted(memory.items()):
        if previous is None or at!=previous+1:
            if buf:chunks.append((start,bytes(buf)))
            start=at;buf=bytearray()
        buf.append(value);previous=at
    if buf:chunks.append((start,bytes(buf)))
    return chunks

packaged=[]
vendor=ROOT/'.pio-core/packages/framework-arduinoadafruitnrf52'
for profile in ('Seeed_XIAO_nRF52840','Seeed_XIAO_nRF52840_Sense'):
    for path in sorted((vendor/'bootloader'/profile).glob('*.hex')):
        strings=[]
        for start,data in image(path):
            strings.extend({'address':hex(start+m.start()),'text':m.group().decode()}
                for m in re.finditer(rb'[\x20-\x7e]{10,}',data)
                if any(s in m.group() for s in (b'Bootloader',b'459adc9',b'Board-ID',b'Seeed',b'lib/nrfx')))
        packaged.append({'file':str(path.relative_to(ROOT)).replace('\\','/'),
            'sha256':sha(path),'identification_strings':strings})

boards=(BOOT/'src/boards/boards.c').read_text()
teardown=boards.split('void board_teardown(void)')[1].split('static uint32_t _systick_count')[0]
assert 'i < NUMBER_OF_PINS' in teardown and 'nrf_gpio_cfg_default(i);' in teardown
main=(BOOT/'src/main.c').read_text()
assert main.index('  board_teardown();')<main.index('  if (bootloader_app_is_valid()')
for profile in ('Seeed_XIAO_nRF52840','Seeed_XIAO_nRF52840_Sense'):
    h=(BOOT/'src/boards'/profile/'board.h').read_text()
    assert re.search(r'#define LEDS_NUMBER\s+0',h)
    assert re.search(r'#define BUTTONS_NUMBER\s+0',h)
    assert not re.search(r'^#define\s+(VBAT|READ_BAT|HICHG|PIN_CHARGING|LED_RGB|LED_NEOPIXEL|LED_APA102)',h,re.M)
    pin=(BOOT/'src/boards'/profile/'pinconfig.c').read_text()
    assert 'bootloaderConfig[]' in pin and not re.search(r'nrf_gpio|PIN_CNF|OUTSET|OUTCLR',pin)
gpio=(OUT/'nrfx-source/nrf_gpio.h').read_text()
body=gpio.split('NRF_STATIC_INLINE void nrf_gpio_cfg_default(uint32_t pin_number)\n{')[1].split('\n}')[0]
assert all(s in body for s in ('NRF_GPIO_PIN_DIR_INPUT','NRF_GPIO_PIN_INPUT_DISCONNECT','NRF_GPIO_PIN_NOPULL'))
for path in (OUT/'nrfx-source/system_nrf52840.c',vendor/'cores/nRF5/nordic/nrfx/mdk/system_nrf52840.c'):
    code=path.read_text()
    assert not re.search(r'PIN_CNF\[(13|14|31)\]',code)
sources=[BOOT/'src/main.c',BOOT/'src/boards/boards.c',BOOT/'src/boards/boards.h',
    *[p for profile in ('Seeed_XIAO_nRF52840','Seeed_XIAO_nRF52840_Sense') for p in (BOOT/'src/boards'/profile).glob('*')],
    *list((OUT/'nrfx-source').glob('*'))]
result={'bootloader_source_commit':'459adc98ea32e0600de550c4a4de9f5ac091d6b6',
    'nrfx_gitlink':'7a4c9d946cf1801771fc180acdbf7b878f270093',
    'packaged_bootloaders':packaged,
    'audited_source_hashes':{str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in sources},
    'installed_bootloader_identified':False,'dirty_binary_source_match_proven':False,
    'electrical_safety_proven':False}
(OUT/'source-audit.json').write_text(json.dumps(result,indent=2)+'\n')
for entry in packaged:
    print(entry['file'])
    for s in entry['identification_strings']:print(s['address'],s['text'])
print('PASS: XIAO board source has no battery-specific pin configuration')
print('PASS: bootloader teardown restores input/disconnected/no-pull defaults')
print('PASS: historical and application SystemInit have no explicit P0.13/14/31 configuration')
print('LIMIT: not a match to installed bootloader; dirty builds and electrical transients unqualified')
