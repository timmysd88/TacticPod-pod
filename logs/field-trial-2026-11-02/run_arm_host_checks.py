"""Run the unchanged host-only scripts with installed ARM GCC and Unicorn.

No compiler install or repository test edits. Substitute only compiler invocation
and execution; retain all original assertions and source/hash checks.
"""
from pathlib import Path
import subprocess, sys, runpy
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tests/tcb3'))
from native import Native

original_run = subprocess.run
compiler = ROOT/'.pio-core/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-g++.exe'

def arm_run(args, **kwargs):
    if args[0] == 'g++':
        args = [str(compiler), '-mcpu=cortex-m4', '-mthumb', '-fno-exceptions',
                '-fno-rtti', '-nostartfiles', '-Wl,-Ttext=0x10000',
                '-Wl,-Tdata=0x20000000', '-Wl,-e,main', *args[1:]]
        print('COMPILER: existing ARM GCC 7.2.1; original warning flags retained', flush=True)
        return original_run(args, **kwargs)
    if len(args) == 1 and Path(args[0]).name in ('button-led-tests', 'standby-tests'):
        code = Native(Path(args[0])).call('main')
        print('ARM EMULATION main return code:', code, flush=True)
        result = subprocess.CompletedProcess(args, code)
        if kwargs.get('check'): result.check_returncode()
        return result
    return original_run(args, **kwargs)

for script in ('button_tests.py', 'standby_tests.py'):
    print('RUN unchanged', script, 'with compiler/execution adapter', flush=True)
    with patch('subprocess.run', arm_run):
        runpy.run_path(str(ROOT/'tests/reusable'/script), run_name='__main__')
