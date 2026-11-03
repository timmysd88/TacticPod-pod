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

# Source guards for the new behaviours.
gps = (root / "src/gps_receiver.cpp").read_text()
assert "TACTICPOD_GPS_BACKUP_STANDBY" in gps and "buildBackupRequest" in gps
power = (root / "src/gps_power.h").read_text()
assert "#define TACTICPOD_GPS_BACKUP_STANDBY 0" in power, "backup standby must default OFF"
# PMREQ is sent before the UART is stopped, never with the USB-only force flag.
sus = re.search(r"void suspend\(\)\s*\{.*?\n\}\n", gps, re.S).group(0)
assert sus.index("buildBackupRequest") < sus.index("Serial1.end()")
# Recording still gates on a verified receiver profile after wake (profile audit restores 10 Hz).
res = re.search(r"void resume\(\)\s*\{.*?\n\}\n", gps, re.S).group(0)
assert "profileRequested=true" in res and "GpsProfile::invalidate" in res
assert "currentVerified()" in gps and "recordingReady" in gps
# Flash is finalised/idle before standby: policy blocks on recording/sessionBusy; flash deep power-down refuses busy parts.
assert "FlashPower::deepPowerDown" in enter and "enterStandby" in main
# USB/serial recovery: VBUS or an open port always keeps the device awake (policy test above); VBUS standby not implemented.
assert "TACTICPOD_STANDBY_ON_VBUS" not in main
polled = (root / "src/standby_policy.h").read_text()
assert "kIdlePollMs = 100" in polled and "kPressedPollMs = 10" in polled
assert "System OFF" not in gps
ini = (root / "platformio.ini").read_text()
assert "tacticpod_reusable_usb_gpsbackup" in ini and ini.count("-DTACTICPOD_GPS_BACKUP_STANDBY=1") == 1

# The established TCB3 storage format is unchanged.
digests = {
    "src/tcb3_store.cpp": "9f0734c9c0d061b3b1b386b0bcc00d31fba5deab7690fdf8fd1b0c6210ef8ca1",
    "src/tcb3_store.h": "1cb2cca79067879880c3a3ba120d013095db27ef2246a425aa69116d2ce6fa2f",
}
for path, expected in digests.items():
    assert hashlib.sha256((root / path).read_bytes()).hexdigest() == expected, path + " changed"
print("PASS: System ON standby policy, flash deep power-down, wake/hold timing, boot never records, TCB3 store unchanged")
