# Stock XIAO software-only battery startup qualification

> **Update 2026-11-02:** Seeed Technical Support has confirmed the P0.14/P0.13/bootloader behaviour (see [seeed-confirmation.md](seeed-confirmation.md)). The bootloader-interval concern and "NO-GO for LiPo connection" below are historical; battery testing now follows [supervised-validation.md](supervised-validation.md) and still needs explicit approval. No electrical measurement by this project is implied.

This review respects the stock-board constraint: no extra circuitry, soldering,
bootloader changes, battery connection or upload. The installed firmware and
recordings remain untouched. The external-monitor proposal is not a requirement
of this software route and is not being implemented.

**Decision: GO for a separately authorised USB-only test, battery absent. The
evidence is not sufficient to qualify battery connection across all startup
states.** This is an evidence limit, not proof that the stock board is defective
or that hardware modifications are inherently necessary.

## Exact firmware correction

Installed framework: `framework-arduinoadafruitnrf52-seeed` 1.10101.0, platform
Seeed 1.1.1+sha.a6fd41d. Board ID `seeed-xiao-afruitnrf52-nrf52840` selects variant
`Seeed_XIAO_nRF52840`. The existing Sense flag retains the proven IMU library path.

There are two distinct HIGH operations:

1. `cores/nRF5/wiring.c::init()` executes `NRF_P0->OUTSET = UINT32_MAX`.
   This preloads every output latch HIGH. Input direction remains input; if
   earlier code leaves output direction set, this can actively drive HIGH.
2. Vendor `variants/Seeed_XIAO_nRF52840/variant.cpp::initVariant()` executes
   `pinMode(VBAT_ENABLE, OUTPUT)` then `digitalWrite(VBAT_ENABLE, HIGH)`.
   The first statement already exposes the preloaded HIGH latch. The original
   comment intends to disable battery sensing.

The prepared correction is in a project-owned **board variant**, not a patch to
generic core `wiring.c`:

```cpp
digitalWrite(VBAT_ENABLE, LOW);
pinMode(VBAT_ENABLE, OUTPUT);
pinMode(PIN_CHARGING_CURRENT, INPUT);
```

Only `tacticpod_reusable_usb` selects `variants/tacticpod` through PlatformIO's
`board_build.variants_dir`. Vendor packages, default firmware and bootloader are
unchanged. The core's all-HIGH latch preload remains and is disclosed; it is
cleared for P0.14 before this variant enables output. This reliably removes the
known variant-induced HIGH from a preceding input state, without claiming to
control a different installed bootloader's inherited output direction.

The linked `main()` calls `init()`, then this `initVariant()`, then creates
tasks/starts the scheduler. USB initialisation and user `setup()` follow later.
Therefore the correction runs before setup and before USB task startup. The
existing application's early `BatteryRuntime::begin()` repeats LOW idempotently.
The pin map, QSPI CS, LEDs, sensor code and charger-current selection are retained.
This follows [Seeed's current P0.14 LOW recommendation](https://wiki.seeedstudio.com/XIAO_BLE/).

## Verification of what was actually linked

`tests/reusable/compiled_startup_tests.py` loads the final PlatformIO ELF into
ARM emulation and executes its **linked core init and linked variant functions**
with modelled peripheral registers. It does not substitute test implementations
of digitalWrite/pinMode. The linked main/init/variant disassembly is archived.

Results:

| Case | Result |
| --- | --- |
| Corrected reusable, initial input state | First variant GPIO operation clears P0.14 latch, then OUTPUT; no active HIGH observed by the register model. |
| Unchanged default, initial input state | Detects the original active HIGH; negative control succeeds. |
| Corrected reusable, deliberately inherited output | Detects earlier core OUTSET HIGH before the variant clears it; hazard is retained in the result. |
| P0.13 at variant completion | INPUT, pull bits disabled. |
| P0.31 direction | No explicit direction change by these functions. |

Counts of HIGH observations in the trace count register writes while HIGH, not
physical pulse counts or measured time. This is not a reset, charger, analog
clamp or power-ramp simulation. Global constructors are inventoried from the
actual linked `__init_array_start/end` range, but are not executed by this test.

## Bootloader source audit and provenance

Historical source was downloaded read-only from the Adafruit repository's Git
network at commit **459adc98ea32e0600de550c4a4de9f5ac091d6b6**, dated 25 November
2021. It includes the original `Seeed_XIAO_nRF52840` and Sense definitions. The
pinned nrfx dependency is **7a4c9d946cf1801771fc180acdbf7b878f270093**; its GPIO
helper, nRF52840 SystemInit and reset assembly were inspected separately.

Sources:

- [Historical board definitions](https://github.com/adafruit/Adafruit_nRF52_Bootloader/tree/459adc98ea32e0600de550c4a4de9f5ac091d6b6/src/boards/Seeed_XIAO_nRF52840)
- [Shared board init/teardown](https://github.com/adafruit/Adafruit_nRF52_Bootloader/blob/459adc98ea32e0600de550c4a4de9f5ac091d6b6/src/boards/boards.c)
- [Bootloader main](https://github.com/adafruit/Adafruit_nRF52_Bootloader/blob/459adc98ea32e0600de550c4a4de9f5ac091d6b6/src/main.c)
- [Pinned Nordic GPIO helper](https://github.com/NordicSemiconductor/nrfx/blob/7a4c9d946cf1801771fc180acdbf7b878f270093/hal/nrf_gpio.h)

Within this snapshot:

- Both XIAO board definitions have zero LEDs/buttons selected and no explicit
  battery-enable/charging-current GPIO configuration. The board-specific
  `pinconfig.c` files contain static UF2 configuration data, not GPIO instructions.
- Shared board init sets clocks/scheduler/timers; the selected XIAO definitions
  do not enable its LED GPIO paths. No battery-pin HIGH or LOW is established.
- After DFU selection/handling, `board_teardown()` runs a loop over all GPIOs
  calling `nrf_gpio_cfg_default(i)` before application start. The pinned helper
  selects input, digital buffer disconnected, no pull. This includes P0.14,
  P0.13 and P0.31. It does **not** hold P0.14 LOW.
- Both historical bootloader SystemInit and the installed application SystemInit
  have no explicit P0.13/14/31 configuration. Reset/runtime startup precedes main.
  SoftDevice/MBR and an installed binary with local changes are not exhaustively
  proven by these source checks.

**This is an available historical source audit, not identification of the
installed bootloader.** No target bootloader was read back, changed or entered.
We did not build a bootloader. A `-dirty` build, if encountered later, would add
untracked source uncertainty; it has not been observed on this particular unit.

The locally packaged XIAO bootloader files were parsed offline with Intel HEX
checksum validation. For each board profile, files named 0.6.1 and 0.6.2 are
byte-identical. Both embed **UF2 Bootloader 0.6.1**, not 0.6.2, with nrfx v2.0.0.
They have no source-commit string establishing a match to the downloaded snapshot.

| Packaged profile, both filenames | SHA-256 |
| --- | --- |
| Non-Sense HEX | `c79c8cf75ebb7abfa53b02fd3584aa9a5aeb8dc1b073f674f4fa162225630c6c` |
| Sense HEX | `ac654c6cab225a933278c8be09b92b41bc4c044d064bd8c9c3314c1c4a0cc8c8` |

Use `logs/stock-battery-review/source-audit.json` as the authoritative hash/string
record. Do not infer the installed version from a package filename or USB VID/PID.

## Charging-current default

P0.13 is Arduino index22; P0.14 is index14 and P0.31 is index32. The corrected
variant and application leave P0.13 INPUT with no pull-up/down, verified against
the actual linked register writes and installed pinMode implementation. Seeed's
current prose specifies nominal **50mA** for this mode and nominal 100mA for LOW
output. Its older HIGH-output example conflicts with the prose and is not used.
No new charger setting or register write is introduced. This preserves the prior
low-current default; it is not a physical charge-current measurement or independent
verification of the Soshine cell specification.

## Remaining electrical limits, with no test equipment

The published circuit connects VBAT through 1M to the P0.31 junction and then
through 510k to P0.14. LOW creates the intended divider; high impedance does not
provide a defined permanent lower-leg sink. Source inspection cannot establish
actual node voltage, leakage/clamping, rail ramp timing, injection current or
transients during reset/DFU/power transitions on this individual board.

[Nordic SAADC operating limits](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/saadc.html)
require AIN inputs within VSS..VDD. The
[I/O absolute maximum](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/abs_max_ratings.html)
is VDD+0.3V for VDD<=3.6V; the ADC's nominal 3.6V full scale is not a pad-voltage
permission. Turning off ADC conversion does not disconnect the battery circuit.
The actual degree/duration of exposure cannot be inferred from the ideal circuit
or the passing software tests, and damage is not established by those tests.

With the stated constraints, this review can fix and verify the software defect
but cannot eliminate that residual electrical uncertainty. No oscilloscope,
hardware modification or bootloader replacement is requested as an action now.
Manufacturer confirmation covering the actual stock-board revision, charging
and reset/DFU conditions would be useful additional evidence if measurements
are unavailable. Such confirmation has not been obtained or invented here.

## Prepared USB-only stage

Archive: `logs/stock-battery-review/tacticpod_reusable_usb-stock-board.zip`.
SHA-256 **ffee03178a49e2b77130da9afdefd5f5c358c6b22ecb3e1e249b2157ca540c67**.
This is the same verified application payload as the preceding corrected build;
no further production-code change was needed. Both reusable/default builds pass.
Reusable static RAM **120836 bytes**, program **189424 bytes**. No added RAM.

The ZIP contains an application update only, no bootloader/SoftDevice image.
It contains reusable TCB3 support with no automatic recording, not a blanket
physical-write-disabled firmware. A future USB-only session must keep RECORD,
OBS, COMP and TCB stopped; no START/erase/destructive diagnostics are proposed.
Battery mode starts absent/unarmed; USB-only nominal ADC readings are not proof
of a battery. Installed firmware is unchanged because this task does not upload.

Seven current targeted suites pass: variant startup, linked-ELF startup, battery
policy, ADC backend, console guards, reusable integration and bootloader-source
audit. The earlier complete 26-suite regression remains archived and its
production-source/firmware hashes still match. Full details and commands are in
`logs/stock-battery-review/REPORT.md`.

For a later separately authorised battery-absent USB test: verify this ZIP hash,
upload the application only, confirm normal GPS/IMU/flash startup and stopped
recorders, check BAT STATUS remains absent/untrusted, and capture bootloader
INFO_UF2.TXT read-only if separately included in that test. Do not enter an update
or bootloader-programming path. That can improve version provenance but still
does not measure P0.31 voltage. No battery connection is part of that stage.
