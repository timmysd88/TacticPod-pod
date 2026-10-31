# Reusable-build startup correction

Only `env:tacticpod_reusable_usb` selects this project-owned variant directory.
The variant name and board ID remain unchanged; other environments select the
vendor variant. Header/pin map, QSPI CS and LED defaults are unchanged.

Copied from `framework-arduinoadafruitnrf52-seeed` **1.10101.0**,
`variants/Seeed_XIAO_nRF52840/`. Original SHA-256:

| File | SHA-256 |
| --- | --- |
| variant.cpp | `e4293e433980627dfe871a5c46115462774ec36a4efbf61fd1f025e7c66dab90` |
| variant.h | `a74818f54005de7f6002b6e2b143686edb9d99a6b98411f469a50960c9d31a0d` |

The executable change replaces P0.14 OUTPUT/HIGH with LOW-latch preload followed
by OUTPUT. P0.13 remains INPUT/no-pull. A compile-time guard rejects use without
the reusable profile. Vendor package files and bootloader are not patched.
Review upstream changes before updating this copy.

This runs after core `init()`, before scheduler/tasks/USB/setup. It cannot control
power ramp, reset, bootloader, constructors or preceding core OUTSET. It does
**not** clear battery connection. See
[battery-startup-safety.md](../../docs/battery-startup-safety.md).
