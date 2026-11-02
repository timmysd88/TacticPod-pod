# Seeed manufacturer confirmation (XIAO nRF52840 Sense battery circuit)

Source: written Seeed Technical Support reply, relayed by the project owner on
2026-11-02. No ticket ID or URL was supplied; the text was not independently
verified by this project. It is a manufacturer statement, **not** a measurement by
us. Applies to the non-Plus XIAO nRF52840 Sense (`Seeed_XIAO_nRF52840` variant).

## Confirmed statements

1. Single-cell 3.7 V lithium battery supported; maximum charge voltage 4.2 V.
2. The factory UF2 bootloader leaves P0.14 as a high-impedance input during
   startup, reset and DFU.
3. Setting P0.14 LOW before enabling OUTPUT is the recommended configuration.
4. P0.31 is not exposed to damaging overvoltage when P0.14 is configured correctly.
5. P0.13 left as a high-impedance input with no pulls selects nominal 50 mA charging.
6. The factory UF2 bootloader should be retained.
7. Seeed recommends its official nRF52 BSP 1.1.9 or newer.

## Effect on earlier documents

The earlier "unresolved bootloader/reset interval" concern (P0.14 possibly HIGH
before application code) is **addressed by statement 2**, so it no longer blocks
firmware development or a supervised battery test. Older documents keep their
historical analysis; where they say NO-GO for LiPo connection, read it as
superseded by this page and gated by [supervised-validation.md](supervised-validation.md).
Simulation-derived voltage bounds in those documents remain model figures only.

## Verified against this repository (source review, no hardware)

| Requirement | Where | Result |
| --- | --- | --- |
| P0.14 LOW before OUTPUT | `variants/tacticpod/Seeed_XIAO_nRF52840/variant.cpp` `initVariant()`; `src/battery_runtime.cpp` `begin()` | `digitalWrite(VBAT_ENABLE, LOW)` precedes `pinMode(..., OUTPUT)`; never driven HIGH |
| P0.13 input, no pulls | same two places | `pinMode(PIN_CHARGING_CURRENT, INPUT)`; never written or pulled |
| Pin mapping | `variant.h`, `g_ADigitalPinMap`, `static_assert` in `battery_runtime.cpp` | 14 -> P0.14, 22 -> P0.13, 32 -> P0.31 |
| Standby | `src/main.cpp` | Does not touch D0, P0.14 or P0.13 |
| Bootloader | `platformio.ini`, board JSON | Unchanged; no linker/bootloader override; application-only image |
| Board | board JSON `seeed-xiao-afruitnrf52-nrf52840` | Non-Plus variant, matches the confirmation's scope |

Enforced in CI by `tests/reusable/startup_tests.py` (variant byte-identity and
P0.14/P0.13 ordering) and `standby_tests.py`. No P0.13, P0.14, charging or
bootloader setting was changed by this documentation update.

## Remaining caveats (not blockers to development)

- **BSP version:** Seeed recommends 1.1.9 or newer; this repo pins platform commit
  `a6fd41d` (framework 1.1.1) for reproducible, tested builds. It is not upgraded
  silently. Moving the pin is a separate change needing a full rebuild and test
  run, then supervised hardware validation.
- Statement 4 depends on correct P0.14 configuration; it does not cover a
  modified board, a different revision or a non-factory bootloader. Confirm the
  bootloader is the factory UF2 (the UF2 drive's `INFO_UF2.TXT` on double-tap reset).
- Our own measurements are still absent: P0.31 voltage, charge current, standby
  current, resistor tolerance, and the 300 mAh cell's protection circuit (from the
  owner, not a datasheet).
- P0.17 charge-status behaviour is unverified and the firmware does not rely on it.
- The sense divider stays enabled in standby (statement 3 / project policy); its
  current draw is unmeasured.
- Calibration is RAM-only: repeat `BAT CAL` after every reset or power loss.
- Lab keeps firmware flashing disabled until bootloader/DFU compatibility is
  verified on the actual unit.
