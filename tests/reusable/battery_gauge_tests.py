import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from host_checks import run_cpp
run_cpp('tests/reusable/battery_gauge.cpp','battery-gauge-tests')
print('PASS: ADC conversion, burst filter, calibration, charge interpolation, categories, hysteresis, trust gating')
