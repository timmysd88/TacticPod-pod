# Reusable firmware battery telemetry

This note applies to `tacticpod_reusable_usb`; it does not change the default
board variant, the bootloader, or TCB3 recording data. No physical calibration,
upload, or battery test is implied.

## Measurement and GPIO

The corrected variant still writes P0.14 LOW before setting it OUTPUT. The ADC
backend samples Arduino D32 (P0.31/AIN7) with 12-bit resolution, the internal
0.6 V reference, gain 1/6, 40 us acquisition, no oversampling, and the 1 MOhm /
510 kOhm divider. The nominal calculation is

```text
Vbat_mV = round(raw_code * 3600 * 1510 / (4095 * 510))
```

`nominal_mV` and `raw_mV` retain the uncorrected ADC-derived voltage. `voltage_mV`
is the latest gain/offset-corrected value; `filtered_mV` is the existing
three-sample median plus IIR output. A reading is valid only while fresh and in
the existing 2.0–4.4 V measurement bounds. The known startup correction does not
qualify reset, power-ramp, or bootloader electrical behaviour.

P0.13 remains INPUT/no-pull (the existing nominal 50 mA charger-current
configuration). P0.17 is sampled only as Arduino D23 INPUT/no-pull. LOW with
VBUS present reports `CHARGING`; HIGH reports
`NOT_CHARGING_OR_FULL`; with VBUS absent the status is `UNKNOWN` because the
indicator may not have a defined level. The public XIAO Sense board map and
Seeed charging-status guidance identify P0.17 as active-low `~CHG`. This GPIO
does not measure current, termination, cell presence, or prove that the exact
board revision/charger is charging. HIGH is never reported as FULL.

## Calibration storage and commands

Existing `BAT CAL <measured_mV>` remains a stopped-only RAM calibration for
compatibility. `BAT CAL SAVE <measured_mV>` is the explicit persistent command.
It requires a fresh ADC sample, an integer reference in 2500–4300 mV, and a
resulting gain between 800000 and 1200000 ppm. The correction is calculated from
the supplied reference and the current raw sample; no calibration value is
preloaded or inferred from historical readings.

Two 4 KiB internal-flash pages (`0xEB000` and `0xEC000`) form a two-copy journal.
The reusable-only linker script ends the application region at `0xEB000`.
Records contain a magic/version marker, sequence, gain, offset, and CRC-32.
Writes erase/program the inactive copy and verify it before the prior valid copy
can be reused. Invalid settings are not applied, and failed/invalid writes leave
the previous valid copy untouched. These addresses are separate from the
external SPI recording flash and the bootloader settings pages. Future firmware
builds must retain the reserved-page linker boundary; a build using the default
linker can overlap this calibration area.

`BAT STATUS` retains all prior key/value fields and adds:

| Field | Meaning |
|---|---|
| `raw_mV`, `voltage_mV`, `filtered_mV` | Uncorrected, corrected, filtered voltage |
| `charge_status` | `CHARGING`, `NOT_CHARGING_OR_FULL`, or `UNKNOWN` |
| `calibration_state`, `calibration_crc_valid` | Calibration state and stored-record integrity |
| `protection_active`, `thresholds_provisional` | Trusted CELL protection state and provisional thresholds |
| `sample_age_ms`, `adc_errors` | Sample age (`N/A` if unavailable) and aggregate invalid-measurement/error count; `adc_timeout` remains separate |
| `protocol=2` | Optional battery-field capability/version marker |

Old fields remain parseable. No device or firmware identity/version response is
added. The four voltage thresholds remain 3600/3500/3300/3700 mV by default;
filtering, the stop delay, hard threshold and recovery hysteresis are unchanged.
Invalid/untrusted voltage cannot start a voltage-based stop. Finalisation remains
independent of USB and does not physically disconnect battery power. CELL mode
is never enabled automatically.

## Lab estimate

Lab may show an **Estimated** percentage only for calibrated, valid voltage,
reported `NOT_CHARGING_OR_FULL`, and an idle recorder. It uses a coarse
interpolated single-cell open-circuit table (3300 mV = 0%, 3700 = 20%, 3800 =
40%, 3900 = 60%, 4000 = 80%, 4200 = 100%) and rounds to 5% increments. It is
unavailable during charging, unknown charger state, recording, uncalibrated or
invalid readings. The result is not a measured SOC and is not used by firmware
protection. System load and time since charge termination are not measured;
remaining operating minutes are intentionally not estimated.

## Multimeter procedure

Use DC volts, a range above 4.2 V, black lead in COM and red lead in V/Ohms.
With the battery connected and serial USB present, wait for `BAT STATUS` to
report `NOT_CHARGING_OR_FULL`; firmware refuses persistent calibration while
the active-low signal indicates charging. This status still does not prove that
charging current is zero. Carefully probe battery positive with red and
negative with black. Never use a current socket or bridge the terminals/pads.
Record a settled value in millivolts and enter only that actual reading in Lab.
Keep all recorders stopped and confirm the persistent write dialog. Do not
proceed if the correct battery contacts cannot be identified safely.

## Verification boundary

The host ADC simulation exercises calibration persistence, CRC rejection,
fallback to the prior valid record, thresholds, stale/invalid samples and
finalisation policy. It does not model NVMC electrical behaviour, real ADC
accuracy, the charger, battery current, USB handover, startup pin transients,
or physical LiPo safety. No physical test is claimed.
