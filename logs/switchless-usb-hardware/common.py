"""One authorised reusable USB-only test; no erase or charging commands."""
import pathlib,time,json,zlib,struct
ROOT=pathlib.Path(__file__).resolve().parents[2]
OUT=pathlib.Path(__file__).resolve().parent
SERIAL_NUMBER='0E8A89B7289430BD'
ALLOWED={'RECORD STATUS','OBS STATUS','COMP STATUS','TCB STATUS','GPS STATUS','DIAG ID','TCB READ','TCB START','TCB STOP','TCB DURATION 10','BAT STATUS','IMU ON','IMU OFF','GPS RATE'}
def save(name,value):
    (OUT/name).write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
def bad(text):
    return any(s in text for s in ('HardFault','HARDFAULT','ASSERT FAILED','RECORD ERROR:',
                                  'TCB FAULT','FLASH ID ERROR','state=8','RECORD STATUS: FAULT'))
class Link:
    def __init__(self,port,logname):
        import serial
        self.p=serial.Serial(port,115200,timeout=.03,write_timeout=1)
        self.log=(OUT/logname).open('wb');self.commands=[]
    def close(self):self.p.close();self.log.close()
    def read(self,seconds,marker=None):
        end=time.monotonic()+seconds;data=bytearray()
        while time.monotonic()<end:
            b=self.p.read(max(1,self.p.in_waiting))
            if b:self.log.write(b);self.log.flush();data.extend(b)
            if marker and marker.encode() in data and b'\n' in data[data.index(marker.encode()):]:break
        text=data.decode('utf-8','replace')
        if bad(text):raise RuntimeError('Device fault: '+text)
        if marker and marker not in text:raise RuntimeError('Response timeout '+marker+': '+text)
        return text
    def send(self,cmd):
        assert cmd in ALLOWED
        self.commands.append(cmd);self.log.write(('\nHOST TX '+cmd+'\n').encode());self.log.flush()
        self.p.write((cmd+'\n').encode());self.p.flush()
    def command(self,cmd,marker,seconds=5):
        self.send(cmd);return self.read(seconds,marker)
    def states(self):
        r={}
        for cmd,marker in [('RECORD STATUS','RECORD STATUS:'),('OBS STATUS','OBS END'),('COMP STATUS','COMP END'),('TCB STATUS','TCB STATUS')]:
            r[cmd]=self.command(cmd,marker)
        assert 'RECORD STATUS: STOPPED' in r['RECORD STATUS'],r
        assert 'capture=OFF' in r['OBS STATUS'] and 'RAM_fast_imu=OFF' in r['OBS STATUS'],r
        assert 'capturing=0 fault=0' in r['COMP STATUS'] and 'input=0/2 output=0/2' in r['COMP STATUS'],r
        assert 'state=1 ' in r['TCB STATUS'] and 'error=none' in r['TCB STATUS'],r
        return r
    def identity(self):
        import re
        t=self.command('DIAG ID','END_FLASH_ID',10)
        assert re.findall(r'FLASH ID JEDEC \d+ ([0-9A-F]+)',t)==['0B4018']*5
        assert 'SR1 before=00 after=00 SR2=02' in t and 'JEDEC_repeat=PASS SFDP_repeat=PASS' in t
        parts=re.findall(r'FLASH ID SFDP pass=(\d+) address=([0-9A-F]+) ([0-9A-F]+)',t)
        assert len(parts)==32
        reference=(ROOT/'logs/phase3b4-hardware/sfdp-capture-1.bin').read_bytes()
        for _,a,h in parts:
            a=int(a,16);assert bytes.fromhex(h)==reference[a:a+16]
        return t
    def export(self,name):
        # The file is exclusive-create; no previous evidence can be overwritten.
        end=time.monotonic()+90;seq=0
        with (OUT/name).open('xb') as f:
            self.send('TCB READ')
            def exact(n):
                data=bytearray()
                while len(data)<n and time.monotonic()<end:
                    b=self.p.read(n-len(data));data.extend(b)
                    if b:f.write(b);f.flush()
                if len(data)!=n:raise RuntimeError('Incomplete TPX3 export: '+name)
                return bytes(data)
            while True:
                h=exact(20)
                if h[:4]!=b'TPX3':raise RuntimeError('Unexpected export bytes: '+repr(h))
                n=struct.unpack_from('<H',h,16)[0];assert n<=256
                tail=exact(n+4);assert zlib.crc32(h+tail[:-4])==struct.unpack_from('<I',tail,n)[0]
                assert struct.unpack_from('<I',h,8)[0]==seq;seq+=1
                if h[5]==3:raise RuntimeError('TPX3 ISSUE packet: '+name)
                if h[5]==4:break
        self.log.write(('\nHOST RX binary saved '+name+'\n').encode());self.log.flush()
        return (OUT/name).read_bytes()
