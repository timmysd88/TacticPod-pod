"""Check retained checkpoint integrity without accessing hardware."""
from host_checks import run_cpp

run_cpp('tests/reusable/boot_diagnostics.cpp', 'boot-diagnostics-tests')
print('PASS: startup checkpoint validity, interrupted writes, previous boot and bounds')
