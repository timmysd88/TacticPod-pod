# Battery startup safety review — 31 October 2026

**NO-GO for LiPo connection/charging. GO for review of a separately authorised,
battery-disconnected USB-only bench stage.** No hardware port was opened, no
firmware uploaded, no battery connected and no physical flash programmed/erased.
Installed working firmware and all three physical recordings remain untouched.

## Finding and exact correction

The board JSON selects the non-Plus `Seeed_XIAO_nRF52840` variant, with the Sense
IMU macro retained. Framework is `framework-arduinoadafruitnrf52-seeed` 1.10101.0.
Battery mapping: Arduino14=P0.14 sense enable, Arduino32=P0.31 AIN7,
Arduino22=P0.13 charge-current selection.

The exact sequence is application reset handler/SystemInit/runtime -> main ->
core init (all GPIO OUT latches HIGH) -> vendor initVariant (P0.14 OUTPUT then
HIGH, intended to disable battery sensing) -> scheduler/USB -> setup -> application
LOW. The HIGH starts on OUTPUT direction change, before the explicit HIGH write.
It is not fixed by making BatteryRuntime::begin the first setup statement.

Only `tacticpod_reusable_usb` now selects a project-owned copy of the variant:
**digitalWrite(14,LOW) before pinMode(14,OUTPUT)**. P0.13 stays INPUT/no pulls.
Pin map/header, QSPI CS and LED defaults are retained. The linker disassembly
confirms LOW-before-OUTPUT; see `initVariant-disassembly.txt` and `variant.diff`.
This removes the known application-image variant interval without changing
vendor package files or bootloader. Normal/default and bounded recording build
configurations retain their original variant.

## Remaining electrical qualification

The subsequent [hardware options decision](../../docs/battery-sense-hardware-options.md)
prefers disconnecting the original VBAT-to-ADC path and using a qualified external
single-cell I2C monitor, such as a reviewed MAX17048 circuit. Merely adding a gauge
does not remove the old path. A permanent grounded divider is a smaller change,
but still needs explicit power-ramp/unpowered-input qualification. No hardware
or further firmware changes were made to implement either option.

See [the detailed review](../../docs/battery-startup-safety.md) for official source
links, exact startup order and proposed bench gates. Seeed's annotated circuit
was downloaded/visually inspected as `seeed-battery-circuit.png`:
https://files.seeedstudio.com/wiki/XIAO-BLE/14.png

With nominal 1M/510k resistors, VBAT4.2V and VDD3.3V, ideal P0.31 is 1.418543V
with P0.14 LOW, 3.603974V with HIGH, and can approach VBAT with the lower leg
floating. These are ideal calculations, not physical voltage measurements.
Nordic specifies AIN inputs within VSS..VDD in operation and ordinary I/O absolute
maximum VDD+0.3V for VDD<=3.6V. ADC nominal 3.6V full scale does not override
pad limits. GPIO reset is input/buffer disconnected/no pull, not a permanent sink.

The application cannot control power ramp/reset/bootloader states. Actual board
revision/bootloader identity and pin transients remain unverified. A future
bootloader change would require separate review; a reset/unpowered electrical
violation may need a hardware change rather than earlier firmware. No bootloader
change or circuit modification is proposed as automatically sufficient.

P0.13 INPUT/no-pull retains Seeed's nominal 50mA mode, approximately 0.167C for
300mAh. This is conditional on the exact cell's permitted charge current and
temperature specification and the actual charger circuit. Neither the claimed
1.2A rating nor pack protection is treated as independent charging qualification.
The wiki's conflicting older HIGH example is not adopted. Charging is autonomous
hardware; unarmed battery software is not a charger disable switch.

## Builds and artifacts

Command (build only):

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e tacticpod_reusable_usb -e seeed_xiao_nrf52840_sense
```

Both environments built successfully; see `builds.txt`.

| Environment | Static RAM | Program bytes |
| --- | ---: | ---: |
| Reusable corrected | 120836 / 237568 | 189424 / 811008 |
| Default TCB3 disabled | 118992 / 237568 | 165336 / 811008 |

No added static RAM or program bytes versus the previous reusable build.
The default ELF is byte-identical to its baseline:
`a83cbaabf9c0e18034a5f70a987539c77c574e429e9b6aa3a06caf6d1a836089`.

Corrected archive: `tacticpod_reusable_usb-battery-startup.zip`

SHA-256: `ffee03178a49e2b77130da9afdefd5f5c358c6b22ecb3e1e249b2157ca540c67`

ELF: `c6f09813857a70bb38fafaa27ef16c1957ae6b63584793d8302bf7afebbc3654`

Application binary: `ea159b3619e3fa27bc59ac9268c8ce8c5630a33b53da82d45901da8a17bdc9ae`

ZIP contains only firmware.bin, firmware.dat and manifest.json (application update,
no bootloader/SoftDevice image). All artifact/source/BSP hashes are saved in JSON.
Old reusable artifacts are preserved, not overwritten.

## Regression validation

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tests/reusable/run_all_tests.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" logs/battery-startup-review/resume_regressions.py
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" logs/battery-startup-review/capture_evidence.py --finalize
```

**All 26 regression suites passed; zero failed.** The original runner completed
17 passing suites before interruption; the remaining nine were resumed and all
passed. See regressions.txt, regressions-resumed.txt, regressions.json and the
archived regression-logs directory. Both test runner processes performed only
host-side simulation. The new startup suite passes six ARM GPIO-model cases
plus electrical-inference/build-isolation checks. It covers
USB-only, battery-present, USB charging, repeated resets and negative controls
for original/inherited HIGH states. It deliberately preserves detection of the
pre-variant hazard. No analog charger or physical reset waveform is simulated.

Source audit confirms no existing src/ production file changed, including ADC,
GPS, recorder and compressor. Vendor BSP and frozen V2 files match prior hashes.
Archived physical image/export hashes and installed engineering ZIP match their
previous values; see `preserved-physical-data.json`.

## Files changed and recovery plan

Changed: platformio.ini (reusable-only variants_dir), tests/reusable/run_all_tests.py,
docs/reusable-prototype.md and docs/battery-bench-checklist.md.

Added: variants/tacticpod/README.md, copied variant.h/cpp, startup.cpp/startup_tests.py
and startup_hw mock headers under tests/reusable/, docs/battery-startup-safety.md,
and this review directory's logs, evidence script and artifacts.

The hardware-options follow-up additionally created
docs/battery-sense-hardware-options.md and linked it from the startup review.
It does not implement a monitor backend or alter the prepared firmware ZIP.

No field rollback is needed because nothing was uploaded. The old installed
bounded movement build remains installed; it and the default build are not
battery-qualified. The earlier reusable ZIP remains archived. Do not use any
archive as battery-connection clearance. Next step is review, then separately
approved USB-only observation with battery absent, followed by resolution of
reset/bootloader electrical limits and pack specification before LiPo connection.
