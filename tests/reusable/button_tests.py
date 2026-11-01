from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="tacticpod-button-") as directory:
    executable = Path(directory) / "button-led-tests"
    subprocess.run([
        "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
        str(root / "tests/reusable/button_led.cpp"), "-o", str(executable)
    ], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: button debouncing/start/stop sequence and nonblocking LED schedules")
