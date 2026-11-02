# Safest practical sensing architecture

> **Update 2026-11-02:** Seeed Technical Support has confirmed the P0.14/P0.13/bootloader behaviour (see [seeed-confirmation.md](seeed-confirmation.md)). The bootloader-interval concern and "NO-GO for LiPo connection" below are historical; battery testing now follows [supervised-validation.md](supervised-validation.md) and still needs explicit approval. No electrical measurement by this project is implied.

Design recommendation only: **no rework, battery connection, charging or upload
is authorised or performed here**. The corrected reusable variant removes the
known HIGH interval, but cannot qualify reset/bootloader states. See the
[startup review](battery-startup-safety.md).

## Recommendation

**Retire the original battery-to-MCU analog path and use a dedicated single-cell
I2C voltage monitor/fuel gauge.** Physically isolate the original path as part of
the change. Merely adding a gauge does not remove the P0.31 exposure.

Proposed topology for separate circuit/rework review:

1. Open **VBAT -> upper 1M resistor -> P0.31**. The published circuit calls that
   resistor R16; match the actual board revision/nets before identifying a part.
   Preserve the power path and separate P0.13 charger-current circuit.
2. Give the isolated ADC junction a physical ground reference, for example a
   reviewed **100kOhm pull-down**. Do not hard-ground P0.14: earlier firmware may
   actively drive it HIGH. The pull-down is not a standalone fix while the upper
   battery resistor remains connected.
3. Evaluate a battery-rated monitor such as **MAX17048**, powered from the
   protected single-cell pack output. This part/module is not assumed purchased
   or qualified. Check the selected breakout schematic, power domains and pins.
4. I2C/alert pull-ups must reference the MCU's **3.3V**, not VBAT. Check for module
   pull-ups, level shifters or push-pull paths that could back-power the unpowered
   MCU. Verify the selected circuit's powered/unpowered I/O behaviour explicitly.

With the original VBAT path disconnected, the existing 510k link to P0.14 plus
the proposed 100k ground resistor gives ideal ADC voltage
`VDD * 100/(510+100)`, about 0.541V at VDD3.3V when P0.14 is HIGH, and zero for
LOW/high impedance. There is no remaining intentional battery feed into P0.31.
This is an inference requiring successful isolation, the published connections
and no solder damage; continuity and voltage measurements must confirm it.

This is small-component board rework, not a generic jumper modification. If
qualified rework is impractical, use a board/carrier with independently qualified
battery sensing or keep this unit USB-only. No software change alone has yet
been demonstrated to cover all reset and power-ramp states on this unit.

[MAX17048 official product information](https://www.analog.com/en/products/max17048.html)
and [datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX17048-MAX17049.pdf)
support a 2.5-4.5V-powered, single-cell I2C voltage monitor. A module's wiring is
not established by the IC datasheet. Use voltage for the initial stop policy;
SOC accuracy/model assumptions need separate validation. This part is not a
charger and does not establish this pack's permitted charge current.

## Alternatives

| Option | Assessment |
| --- | --- |
| Reusable variant LOW-before-OUTPUT | Keep as defence in depth. Cannot control reset/bootloader/power ramp; insufficient alone. |
| Pull-down on P0.14 | Cannot override an actively driven HIGH safely; incomplete fix. |
| Hard-ground P0.14 | Reject: potential short against HIGH output. |
| Disconnect lower 510k resistor from P0.14 and permanently ground its free end | Minimal-rework candidate; removes GPIO dependency and retains nominal divider ratio. ADC still sees about 1.419V from a full cell while VDD is absent/ramping, so power-sequencing/injection qualification is still required. |
| Resistor added at P0.31 with upper battery resistor intact | Reduces voltage and changes calibration, but still provides a battery-derived voltage with VDD absent. Insufficient by itself. |
| Clamp to VDD or generic analog switch | Clamps can back-power the MCU; ordinary switches need not tolerate powered-off inputs. Requires a complete circuit review. |
| Disable ADC/software readings only | Electrical battery path remains. Not a safety fix. |
| Isolate original path plus qualified external monitor | Preferred robust architecture; no reliance on P0.14 boot state, while preserving voltage-based finalisation. |

A permanent grounded divider could be the cheapest correction if measurements
and manufacturer limits establish acceptable power sequencing on this board.
That evidence is absent, so it is not the preferred safety recommendation now.

## Later firmware work, only after design approval

Use the existing battery-monitor abstraction with a new backend. Disable the
onboard ADC backend for that profile: its deliberately grounded ADC node must
not be treated as an empty battery. Explicitly identify the voltage source;
unavailable, stale or unconfigured readings remain untrusted. Preserve GPS/IMU,
TCB3, flash guards and export.

Review free I2C pins/bus routing and bounded access before implementation.
MAX17048 active voltage updates are specified at 250ms, versus 45s in hibernate;
polling at 1Hz does not prove fresh conversions. Qualify the selected operating
mode and stale-data handling before using the current short shutdown thresholds.
Test absent cell, USB-only, reset, NACK/stuck bus and cached readings. No backend
or monitor configuration writes were implemented in this task.

P0.13 remains INPUT/no-pull for proposed nominal 50mA charging, conditional on
the exact pack and charger specification. Sensing isolation does not qualify
charging, polarity or temperature protection.

**NO-GO for battery connection/charging now.** Review the circuit/rework plan
first; the preferred architecture requires no bootloader modification.
