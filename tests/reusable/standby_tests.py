from pathlib import Path
import hashlib
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
from host_checks import run_cpp
run_cpp('tests/reusable/standby.cpp','standby-tests')

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

# The TCB3 store is pinned: the fast-reset epoch/marker change is the only deliberate revision.
digests = {
    "src/tcb3_store.cpp": "32ab377c7ad21ff678dea1e3a313803a34daeabd01d49fc7f1f9543087dc8607",
    "src/tcb3_store.h": "ec640ca2f6f3b2c6af9dd462323c955a0a9d717532dde5cd49f2ec03d1fd8645",
}
for path, expected in digests.items():
    assert hashlib.sha256((root / path).read_bytes()).hexdigest() == expected, path + " changed"
print("PASS: System ON standby policy, flash deep power-down, wake/hold timing, boot never records, TCB3 store unchanged")
