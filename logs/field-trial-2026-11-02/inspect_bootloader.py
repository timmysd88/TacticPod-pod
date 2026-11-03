"""Enter existing bootloader by 1200-baud touch, read identity; never upload."""
from pathlib import Path
import json, time, serial, string
from serial.tools import list_ports

OUT = Path(__file__).resolve().parent
pre = json.loads((OUT/'preservation-result.json').read_text())
assert pre['passed'] and pre['serial_released']
number = pre['device']['serial']
devices = [p for p in list_ports.comports() if p.vid == 0x2886
           and p.pid == 0x8044 and p.serial_number == number]
assert len(devices) == 1, 'Application identity changed; stop'
result = {'uploaded': False, 'bootloader_modified': False, 'touch_attempted': False}
try:
    with (OUT/'bootloader-inspection-attempt.json').open('x') as target:
        json.dump({'method':'existing USB CDC 1200-baud touch', 'port':devices[0].device}, target)
    result['touch_attempted'] = True
    with serial.Serial(devices[0].device, 1200, timeout=.1) as touch:
        touch.dtr = False
    end = time.monotonic() + 20
    found = []
    info = []
    while time.monotonic() < end:
        found = [dict(port=p.device, vid=p.vid, pid=p.pid, serial=p.serial_number)
                 for p in list_ports.comports() if p.vid == 0x2886 and p.serial_number == number]
        info = []
        for letter in string.ascii_uppercase:
            path = Path(letter+':/INFO_UF2.TXT')
            try:
                if path.is_file(): info.append(dict(path=str(path),text=path.read_text()))
            except OSError: pass
        if info and found and all(p['pid'] != 0x8044 for p in found): break
        time.sleep(.2)
    result['ports'] = found
    result['uf2_info'] = info
    print(json.dumps(result, indent=2), flush=True)
finally:
    (OUT/'bootloader-inspection.json').write_text(json.dumps(result, indent=2)+'\n')
