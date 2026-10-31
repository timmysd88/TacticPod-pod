# Battery sensing startup safety review

**NO-GO for connecting or charging the LiPo.** The known variant HIGH interval
is corrected in the separate reusable build. Reset, power ramp and bootloader
behaviour remain electrically unqualified. **GO for review of a separately
authorised USB-only bench test with the battery absent.** No upload, serial-port
access, battery connection, physical flash write/erase or bootloader change was
performed for this review.

## Exact installed core and sequence

Board `seeed-xiao-afruitnrf52-nrf52840` selects `Seeed_XIAO_nRF52840`, not the
separate Sense variant. The existing Sense macro selects the working IMU library
path. Both installed variants agree on battery pins. Framework package is
`framework-arduinoadafruitnrf52-seeed` **1.10101.0** under
`.pio-core/packages/framework-arduinoadafruitnrf52/`.

| Stage | Source relative to package | Battery-relevant behaviour |
| --- | --- | --- |
| Power ramp/reset/bootloader | Installed bootloader not identified/read back here | Application has no control. Pack attachment can apply VBAT immediately. |
| Application reset handler | `cores/nRF5/linker/gcc_startup_nrf52840.S` | Runtime memory startup, `SystemInit()`, `_start`/construction before main. |
| `main()` calls `init()` | `cores/nRF5/main.cpp`, `wiring.c` | Clock/RTC initialisation, then **P0/P1 OUTSET = UINT32_MAX**. All output latches HIGH; direction is not changed by OUTSET. Inherited output direction could drive HIGH here. |
| `main()` calls vendor `initVariant()` | `variants/Seeed_XIAO_nRF52840/variant.cpp` | **pinMode(14, OUTPUT)** exposes the HIGH latch, then **digitalWrite(14, HIGH)** retains it. Original comment says disable battery reading. |
| Same vendor variant | As above | P0.13 INPUT/no-pull selects low current; QSPI CS/LED defaults follow. |
| Scheduler -> loop task | `main.cpp::loop_task` | TinyUSB initialisation, optional debug serial, then setup. |
| Application setup | `src/main.cpp`, `src/battery_runtime.cpp` | First setup action calls `BatteryRuntime::begin()`: LOW latch before OUTPUT and P0.13 INPUT. Too late to prevent the original interval. |

There is no measured millisecond duration for that HIGH interval. The installed
`SystemInit()` has no explicit P0.13/14/31 configuration; this does not establish
SoftDevice, bootloader or inherited pin behaviour. Local packaged 0.6.1/0.6.2
bootloader images do not prove which bootloader is installed. No bootloader
source/image was modified or uploaded.

## Exact prepared correction

Only `env:tacticpod_reusable_usb` now selects
`board_build.variants_dir = variants/tacticpod`. The project-owned variant retains
the original header, pin map, QSPI CS and LED behaviour. Battery initialisation is:

```cpp
digitalWrite(VBAT_ENABLE, LOW); // Clear HIGH latch before enabling output.
pinMode(VBAT_ENABLE, OUTPUT);  // P0.14 sinks divider, no HIGH interval here.
pinMode(PIN_CHARGING_CURRENT, INPUT); // P0.13, no pulls, low-current mode.
```

The application repeats this policy idempotently. A compile guard restricts the
variant to the reusable profile. Vendor sources, default environment, firmware
board identity, linker/USB settings and bootloader selection remain unchanged.
Recorder, GPS, ADC implementation, flash guards and frozen binary formats are
unchanged. The compiled ELF was inspected: its `initVariant()` first calls
`digitalWrite(14,0)`, then `pinMode(14,1)`, then `pinMode(22,0)`.

Setup-only code cannot prevent the original earlier HIGH. This variant override
removes that application-image interval without patching the installed core.
It cannot make VBAT absent until firmware runs. If actual bootloader activity
needs correction, that requires separate review/approval. If reset or unpowered
states violate pin limits, even an earlier bootloader instruction is insufficient;
a separately reviewed hardware solution may be required. Default firmware still
uses the vendor behaviour and is not battery-qualified.

## P0.31 limits and the uncontrolled interval

[Seeed's guidance](https://wiki.seeedstudio.com/XIAO_BLE/) requires P0.14 LOW for
sensing and warns against HIGH during charging. The
[annotated original battery circuit](https://files.seeedstudio.com/wiki/XIAO-BLE/14.png)
and [published schematic](https://files.seeedstudio.com/wiki/XIAO-BLE/Seeed_Studio_XIAO_nRF52840_PDF.pdf)
show VBAT -> 1M resistor -> P0.31 sense junction -> 510k resistor -> P0.14.
This identifies the published topology, not this individual board revision or
resistor values. The original diagram labels BQ25100; the revised PDF labels
BQ25101YFPT. Actual assembly remains to be established.

Ideal nominal-resistor inference at VBAT=4.2V, VDD=3.3V:

| P0.14 state | Inferred P0.31 voltage |
| --- | --- |
| LOW | `4.2 * 510 / 1510 = 1.418543V` |
| HIGH at 3.3V | `(4.2 * 510 + 3.3 * 1000) / 1510 = 3.603974V` |
| High impedance | Lower leg unsunk; junction can approach VBAT, not a defined safe divider. |

These are calculations, **not physical measurements**. Leakage, clamps, loading,
tolerances and transients are not modelled. Clamp conduction is not an approved
operating mode; high series resistance alone does not establish compliance.

[Nordic's absolute maximum ratings](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/abs_max_ratings.html)
limit ordinary I/O to VDD+0.3V when VDD<=3.6V. This damage limit is not a design
target and follows the instantaneous supply during ramp. The ADC's nominal 3.6V
full scale does not make P0.31 tolerant of that voltage at every VDD.
[Nordic SAADC input requirements](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/saadc.html)
also require AIN0..7 within **VSS..VDD during operation**, stricter than the
absolute damage limit. With the sink LOW and VDD stable at 3.3V, the nominal
1.419V is within that range. The divider's nominal Thevenin resistance is about
338kOhm; the existing 40us acquisition setting allows up to 800kOhm according
to Nordic's table. This supports the selected acquisition setting, not a claim
of calibrated accuracy or safety during supply ramp.
[GPIO reset PIN_CNF=0x00000002](https://docs.nordicsemi.com/r/bundle/ps_nrf52840/page/gpio.html)
means input, digital buffer disconnected, no pull. Digital-buffer disconnection
does not disconnect the physical pad or its protection structures. Reset does
not supply a permanent lower-leg sink.

The variant correction therefore does not prove safety during reset held,
prolonged DFU, power ramp/loss, brownout, USB transitions or watchdog reset.

## Separate P0.13 charging policy

P0.13 is Arduino index22, separate from P0.14 index14 and P0.31 index32. Seeed's
current prose specifies INPUT/high impedance with no pulls for nominal **50mA**,
and OUTPUT LOW for nominal 100mA. Installed `pinMode(INPUT)` explicitly disables
pulls. Preserve INPUT in both controlled startup and runtime; add no higher-current
command. The wiki's older HIGH-output example conflicts with its current prose
and is not adopted.

50mA/300mAh is approximately **0.167C**, the proposed conservative board mode,
conditional on this exact pack's verified charge-current/temperature specification
and actual charger circuit. No charge current or voltage has been physically
measured here. The user-reported Soshine602030 protection, 1.2A maximum, 4.2V
charge voltage and 2.75V discharge cutoff are not an independently verified pack
datasheet. Capacity/protection alone does not authorise charging. P0.17/LED/USB
presence cannot prove cell presence, current, termination or thermal safety.
The published circuit uses a fixed TS resistor; pack-temperature sensing is not
qualified. Firmware declining to initialise the monitor does not disable the
hardware charger. P0.13's earlier boot/reset states are also unqualified.

## Tests and limitations

`tests/reusable/startup_tests.py` executes the **actual corrected ARM variant**
with GPIO model backends. Cases cover USB-only, battery-present and USB-charging
contexts, ten simulated resets, inherited-output-HIGH hazard and original-variant
negative control. Checks include LOW-before-OUTPUT, P0.13 no-pull INPUT, unchanged
pin map/header, QSPI/LED state, untouched ADC pin, core call order and build
isolation. Supply contexts have identical GPIO requirements; this is not an
analog charger simulation. Negative controls deliberately detect pre-variant
hazards rather than certify the entire boot. Ideal divider tests flag HIGH and
high-impedance risk. Existing ADC/policy/integration tests cover USB-only untrusted
readings, calibration gates, drain/finalisation and interrupted recovery.

Builds, full regression results and corrected hashes are archived under
`logs/battery-startup-review/`. Original `logs/reusable-review/` evidence remains
preserved as the previous review baseline. See the new report for final results.

## Proposed next stage, battery still absent

The follow-up [hardware options review](battery-sense-hardware-options.md)
recommends isolating the original battery-to-ADC path and using a qualified
external voltage monitor; it explains why a pull-down or grounded divider alone
does not establish safety through every power state. No hardware was changed.

1. Review the correction/hash and separately authorise USB-only upload/read-only
   verification. All recorders stay stopped; preserve all three physical sessions.
2. Match the physical board revision and identify/qualify the installed bootloader.
   Observe P0.14, P0.31 and VDD together through startup, prolonged DFU, reset held,
   repeated resets and USB transitions using an appropriately qualified fixture.
   Megohm divider loading matters: ordinary probes/meters can change readings.
   Do not inject voltage at the sense junction or emulate a battery without a
   separately reviewed electrical procedure.
3. Resolve the floating lower leg during reset/power ramp. Any added sink/clamp or
   circuit change needs worst-case voltage/current and calibration review; no
   resistor change or bootloader patch is assumed authorised or sufficient.
4. Obtain the exact pack specification and verify polarity, charger circuit and
   thermal requirements before reviewing LiPo connection. Follow the battery bench
   checklist only after all gates pass.

Until these are closed: **NO-GO for LiPo connection or charging.**
