# Offline provisional TCB3 discovery experiment

The Phase 4A reference prototype remains under `tests/reusable`. Phase 4B
added an independent component under `src/tcb3_fast_discovery.*`. Phase 4C
connects it to the explicitly negotiated experimental BLE path described in
[the session download protocol](ble-session-download-protocol.md#experimental-provisional-discovery-phase-4c).
Legacy LIST and GET and the recording format remain unchanged. The scanner
does not write flash.

The scanner reads the reset marker, sector prefixes, sealed TCB3 headers and
reserved footer pages. It checks header metadata and CRC, session and sector
identity, epoch, plausible page-sequence progression, footer CRC, flags, time
and offered/committed/lost accounting. Every returned entry is explicitly
provisional and has no manifest validation flags.

It reports complete discovery, incomplete sessions, corrupt metadata,
ambiguous metadata, overflow and I/O errors separately. Corrupt, ambiguous,
overflow and I/O results expose no partial candidate list. An incomplete
session is counted but not offered. As in the existing unmarked-flash scan,
an all-FF 16-byte header prefix is skipped; hidden non-FF bytes after such a
prefix cannot be detected by this prototype. A sealed different-epoch header
terminates the current-epoch walk. Unknown or unsealed metadata fails closed.

This is **not** content validation. The scanner never reads data pages, cannot
prove that footer counts correspond to blocks, and cannot detect data-page or
block corruption. A complete discovery result means only that the metadata
scan finished. Valid loss-marked footers are candidates, not clean recordings.
The negotiated GET path requires existing full validation through the selected
range on the pod and the existing strict host validation. Old LIST and GET
semantics remain intact.

Run the ARM/Unicorn tests with:

```
python tests/reusable/fast_discovery_tests.py [path-to-verified-usb-export.tpx3]
```

The optional export is decoded read-only; because TPX3 does not export the reset
marker, the test models a marker from the common recorded epoch. That modeled
image is not a raw flash backup. The tests compare candidates with the existing
full validator, count steps/reads/bytes, assert flash remains unchanged, and
exercise torn or invalid metadata, incomplete sessions, overflow, and I/O
failure. Physical timing and responsiveness are not measured.

On the verified three-session export with a modeled epoch-6 marker, the
prototype found sessions 2, 1719 and 1722 in 15,516 steps and 882,688 read
bytes. The same test image took the existing full validator 112,001 steps and
7,057,680 read bytes. These are emulator operation counts, not hardware timing.

## Phase 4B scheduler contract

The scanner starts with no usable result. `begin` initializes its state;
`update(false)` performs no flash or status operation. An `update(true)` does
at most one status check and one read of at most 64 bytes, plus bounded metadata
and CRC work. This is an incremental interface, not a hard real-time guarantee.

No ownership, flash wake/sleep or scheduling policy is wired into the component.
Its caller must obtain the existing flash ownership/admission guard, keep
the chip awake and idle for every allowed step, honor the current service
budget and restart the scanner if the storage changes. Admission must cover the
entire status/read transaction; passing `true` without that guard is unsafe.
The component cannot itself detect a mutation between steps. Errors and
overflow expose no partial candidate list, and entries never carry validated
manifest flags.

The native parity test is:

```
python tests/reusable/fast_discovery_production_tests.py [path-to-verified-usb-export.tpx3]
```

Before Phase 4C, the unreferenced component was removed by linker garbage
collection. A scanner instance requires 8,816 bytes under the tested ARM ABI;
Phase 4C reuses storage with the full manifest rather than allocating both.
Neither scheduler latency nor linked-code behavior has been physically tested.
