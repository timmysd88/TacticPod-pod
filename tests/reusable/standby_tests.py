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

# Pin the reviewed TCB3 store, including metadata, reset-epoch scan-start updates and the aligned readPage export path.
digests = {
    "src/tcb3_store.cpp": "950d55e3e5c661bb06acd74942f61af0236563fa5e778297462a406bd0d8ab8a",
    "src/tcb3_store.h": "814715b9b4ab5bd352f6d129baee8206b2330debfed0f66bac98ac8e39418f06",
}
for path, expected in digests.items():
    # Line endings are normalised so a CRLF working tree matches the committed LF blob.
    data = (root / path).read_bytes().replace(b"\r\n", b"\n")
    assert hashlib.sha256(data).hexdigest() == expected, path + " changed"
print("PASS: System ON standby policy, flash deep power-down, wake/hold timing, boot never records, and TCB3 storage pin")
