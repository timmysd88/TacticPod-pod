# Stock-board software route review

**GO: separately authorised USB-only testing with battery absent. Battery
connection is not qualified by the available evidence.** No upload, COM access,
battery connection, flash programming/erase or hardware/bootloader change occurred.
No additional circuitry or specialist equipment is assumed available.

See the [complete technical assessment](../../docs/stock-board-battery-startup.md)
for source paths, original/revised statements, bootloader evidence, electrical
limits, charging mode and proposed USB-only checks.

## What is established

- Generic core init preloads all GPIO latches HIGH. Original variant OUTPUT/HIGH
  drives P0.14 HIGH. Corrected reusable variant clears LOW before OUTPUT, before
  tasks/USB/setup. It does not alter generic wiring.c or bootloader.
- Linked ELF execution with GPIO register modelling verifies the actual selected
  functions, including a default-firmware negative control and inherited-output
  hazard case. Disassembly of main/init/initVariant is archived. Constructors are
  inventoried, not executed by that targeted test.
- Available bootloader source at commit 459adc98ea32e0600de550c4a4de9f5ac091d6b6
  and pinned nrfx helpers show no XIAO battery-specific drive; teardown restores
  GPIO input/disconnected/no-pull before application entry. This leaves an
  uncontrolled high-impedance interval, not a guaranteed LOW.
- Packaged 0.6.1/0.6.2 HEX pairs are identical for each XIAO profile and both embed
  a 0.6.1 version string. Installed bootloader identity/source match is unknown.
- P0.13 remains INPUT/no pulls, proposed nominal 50mA per Seeed guidance. Neither
  real charge current nor the cell's maximum permitted current was verified.

## Build and regression evidence

Executed commands:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e tacticpod_reusable_usb -e seeed_xiao_nrf52840_sense
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" logs/stock-battery-review/run_validation.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" logs/stock-battery-review/package_review.py
```

Both builds succeeded. **Seven current targeted suites passed, zero failed.**
The earlier complete **26-suite** result is preserved at
`logs/battery-startup-review/regressions.json`. Its production source/config/variant
hashes and final firmware remain unchanged; no new firmware behaviour was added
by this audit. A repeated full storage/compressor test run was not necessary for
unchanged firmware. The current integration run again checks prior-session
preservation, simulated power interruption/recovery, USB-only handling, low
battery finalisation, overflow/fault locking, TIMER4 rollover and export isolation.

Test/audit commands, logs and results: validation-run.txt, regressions.json,
compiled-startup-results.json and source-audit.json. The linked-function test
executes real compiled code with modelled registers, not actual hardware.
It neither models charger electrical behaviour nor qualifies reset waveforms.

| Artifact/resource | Result |
| --- | --- |
| ZIP | tacticpod_reusable_usb-stock-board.zip |
| ZIP SHA-256 | `ffee03178a49e2b77130da9afdefd5f5c358c6b22ecb3e1e249b2157ca540c67` |
| ELF SHA-256 | `c6f09813857a70bb38fafaa27ef16c1957ae6b63584793d8302bf7afebbc3654` |
| Static RAM | 120836 / 237568 bytes |
| Program storage | 189424 / 811008 bytes |
| Default ELF | Unchanged: `a83cbaabf9c0e18034a5f70a987539c77c574e429e9b6aa3a06caf6d1a836089` |

The application-only ZIP has no bootloader image. Reusable firmware retains
explicit TCB3 recording capability, with no automatic recording at boot. It is
prepared for a stopped, USB-only bench session, not re-labelled as a build that
cannot ever issue flash writes. No recording/erase commands were sent.

## Files created/changed

Added tests/reusable/compiled_startup_tests.py, docs/stock-board-battery-startup.md,
and this review directory's source snapshot, pinned Nordic source extracts,
metadata, audit/validation/packaging scripts, logs and application artifacts.
Updated docs/reusable-prototype.md with a link to this stock-board assessment.
No production .cpp/.h, PlatformIO configuration, vendor core, bootloader or flash
format changed during this task. All prior firmware/archive files were retained.

## Remaining decision

The stock-board software route is supported for removing the known variant HIGH
and proceeding to USB-only functional verification. It does not establish
P0.31/VDD behaviour during reset, prolonged bootloader, power ramp, brownout or
USB transitions, and it does not identify the actual installed bootloader.
Passing simulations are not battery-connection clearance. The evidence also
does not establish that the stock board will be damaged or requires rework.

With measurements unavailable, source inspection alone cannot close that gap.
Read-only bootloader identity capture on USB can improve provenance; confirmation
from Seeed covering this board revision and these power/reset conditions could
provide further evidence without modifying hardware. Neither has been obtained
or substituted by an assumption. No equipment purchase, soldering or bootloader
change is proposed as a task action. Battery remains disconnected pending review.
