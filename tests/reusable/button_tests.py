from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
from host_checks import run_cpp
run_cpp('tests/reusable/button_led.cpp','button-led-tests')

print("PASS: button debouncing/start/stop sequence and nonblocking LED schedules")
