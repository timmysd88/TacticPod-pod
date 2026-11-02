"""Run portable C++ checks using host GCC or the pinned ARM emulator toolchain."""
from pathlib import Path
import shutil, subprocess, sys, tempfile

def run_cpp(source, name):
    root=Path(__file__).resolve().parents[2]
    if shutil.which('g++'):
        with tempfile.TemporaryDirectory(prefix='tacticpod-') as folder:
            executable=Path(folder)/name
            subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror',str(root/source),'-o',str(executable)],check=True)
            subprocess.run([str(executable)],check=True)
    else:
        sys.path.insert(0,str(root/'tests/tcb3'))
        from native import compile,Native
        elf=compile(source,name,extra=('-Wall','-Wextra','-Werror','-Wl,-e,main'))
        result=Native(elf).call('main')
        assert result==0,(name,result)
        print('Pinned ARM GCC + Unicorn fallback (host g++ unavailable)')
