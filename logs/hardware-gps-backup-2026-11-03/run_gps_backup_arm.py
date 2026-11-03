"""Run the unchanged GPS suite with newlib no-OS syscall stubs for host ARM emulation.

The repository's fallback links stdio without syscall stubs. This adapter changes
only this test process's link flags, never firmware or repository test sources.
"""
from pathlib import Path
import runpy
import sys

root = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(root / 'tests/reusable'), str(root / 'tests/tcb3')]
import native

original_compile = native.compile

def compile_with_nosys(source, name, includes=(), extra=()):
    return original_compile(source, name + '-nosys', includes,
                            (*extra, '--specs=nosys.specs'))

native.compile = compile_with_nosys
runpy.run_path(str(root / 'tests/reusable/gps_backup_tests.py'), run_name='__main__')
