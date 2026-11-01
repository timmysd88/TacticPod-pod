from pathlib import Path
import hashlib
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="tacticpod-standby-") as directory:
    executable = Path(directory) / "standby-tests"
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    str(root / "tests/reusable/standby.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

main = (root / "src/main.cpp").read_text()
# The recorder is only ever started by the button path inside updateFieldControls.
assert main.count("startFromButton()") == 1
body = re.search(r"void setup\(\)\s*\{.*?\n\}\n", main, re.S).group(0)
assert "startFromButton" not in body and "TCB START" not in body, "boot must never record"
enter = re.search(r"bool enterStandby\(\)\s*\{.*?\n\}\n", main, re.S).group(0)
for forbidden in ("VBAT_ENABLE", "PIN_CHARGING_CURRENT", "NRF_POWER", "SYSTEMOFF", "sd_power"):
    assert forbidden not in enter, forbidden
assert "SYSTEMOFF" not in main and "sd_power_system_off" not in main, "System OFF is intentionally not implemented"
for path in ("src/standby_policy.h", "src/flash_power.h"):
    text = (root / path).read_text()
    assert not re.search(r"0x(06|20|02|C7|60|D8)\b", text), "standby helpers must not use erase/write opcodes"

# The established TCB3 storage format is unchanged.
digests = {
    "src/tcb3_store.cpp": "9f0734c9c0d061b3b1b386b0bcc00d31fba5deab7690fdf8fd1b0c6210ef8ca1",
    "src/tcb3_store.h": "1cb2cca79067879880c3a3ba120d013095db27ef2246a425aa69116d2ce6fa2f",
}
for path, expected in digests.items():
    assert hashlib.sha256((root / path).read_bytes()).hexdigest() == expected, path + " changed"
print("PASS: System ON standby policy, flash deep power-down, wake/hold timing, boot never records, TCB3 store unchanged")
