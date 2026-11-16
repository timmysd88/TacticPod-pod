# Phase 1: runtime factory device identity

Implementation scope: `feature/unique-device-identity`. No provisioning, per-device build, USB descriptor change, bootloader change or application change is required. Factory identity is an identifier, not authentication or a human product serial.

## Browser-compatible BLE identity addition

The follow-up `feature/ble-identity-gatt-fix` adds a read-only characteristic to
the existing TacticPod service:

- Service: `b9730001-7d2a-4f91-a380-8b52c029dc61`.
- Hardware identity: `b973000c-7d2a-4f91-a380-8b52c029dc61`.
- Value: exactly 16 lowercase ASCII hex bytes, including leading zeroes, with no
  NUL terminator, newline, framing or byte-order conversion.
- Scheme: `nrf52840-ficr64-v1`, using the existing FICR helper and the same full
  UID as DIS/USB. This is live hardware identity, not a stored session's device ID.
- Properties/permissions: READ only, open read, no client writes, fixed length 16.
  Initialization failure or a short value write prevents advertising.

The next unused UUID suffix after the existing `000b` feature capability was
chosen; all existing UUIDs and the service permission remain unchanged.
Identity is populated once during GATT initialization and has no flash, recording,
manifest or export ownership dependency. It fits an ATT MTU-23 read response.

BLE remains mutually exclusive with recording under the existing lifecycle:
the characteristic is always readable whenever GATT is active, but this addition
does not enable BLE during active recording or change power/interlock behaviour.
The standard DIS Serial Number (`0x2A25`) remains unchanged for native clients.
Web Bluetooth blocklists that standard characteristic, so browsers must read the
custom identity instead. DIS Firmware Revision (`0x2A26`) remains optional metadata.
The app must preserve exact uint64 handling and recording-identity mismatch checks.

## Canonical identity

`src/device_identity.h` reads both nRF52840 FICR DEVICEID words. The existing interpretation is preserved:

```
uint64 = DEVICEID[0] | (uint64(DEVICEID[1]) << 32)
scheme = nrf52840-ficr64-v1
```

Hex is exactly 16 lowercase digits, including leading zeroes. Decimal is an exact string, never a floating-point number. For example, words `[0x289430bd, 0x0e8a89b7]` produce hex `0e8a89b7289430bd` and decimal `1047801283069882557`. These are illustrative values, not a provisioned pod identity. IDs above 2^53 must remain strings in JavaScript (or be parsed using BigInt).

The full DIS Serial Number retains its existing 16-character hex format. The TPX3 metadata field retains its existing little-endian uint64 bytes at metadata offset 16 and device-present flag at offset 68. Scheme text is not added to TPX3 and the recording format/version, session identifiers, sensor sensitivities and recording behaviour are unchanged. Old recordings are not rewritten. A missing device-present flag must not be interpreted as verified device identity.

On targets without NRF_FICR the helper returns unavailable; it never fabricates a zero ID. Zero-valued words remain representable for formatting tests.

## BLE display name and limits

Display name: `TacticPod-` followed by the last eight lowercase UID hex digits, for example `TacticPod-289430bd`. It is 18 characters. The installed Bluefruit legacy advertising implementation has separate 31-byte advertising and scan-response buffers. Existing advertising contains flags (3 bytes) and the existing 128-bit custom service (18 bytes): 21 bytes. The complete name is placed in scan response, consuming 20 bytes including AD length/type. Compile-time assertions and tests constrain these sizes. Bluefruit setName copies the stack-local string into the SoftDevice name.

The app's existing service filter and `namePrefix: 'TacticPod'` remain compatible. Existing service/GATT UUIDs, protocol 2.0, packet framing, address configuration, connection/advertising timing and timeout values are unchanged; the browser-compatible addition above is a separate read-only characteristic. No BLE address or privacy setting is changed.

The suffix is deliberately display-only. With uniformly distributed lower 32 bits, birthday collision probabilities are approximately 0.0116% for 1,000 pods and 1.16% for 10,000; FICR low-word distribution is not qualified here. Runtime tests include distinct full UIDs with identical suffixes. Always use the full DIS/CDC/recording ID to match a device. If duplicate names appear in a chooser, power/advertise one pod at a time and verify its full ID; the name alone cannot resolve it. No promise of globally unique display names is made.

## USB CDC command

Send `DEVICE INFO` followed by Enter at the existing console baud, 115200, in normal USB/idle mode. The existing BLE-mode main loop does not service USB commands; leave BLE mode through its existing controls before querying CDC. CRLF and LF follow the existing parser. A successful BLE-build reply is one bounded UTF-8/ASCII JSON line (with a leading newline for stream resynchronisation):

```json
{"type":"device_info","version":1,"uid_hex":"0e8a89b7289430bd","device_id":"1047801283069882557","scheme":"nrf52840-ficr64-v1","firmware":"ble-download-2"}
```

`firmware` reuses the existing BLE DIS firmware revision, not a newly assigned release number. Other environments currently have no shared version and return JSON `null`. Unsupported targets return `{"type":"device_info","version":1,"status":"unavailable"}`. Consumers must verify type/version and require all identity fields before matching devices. A future command parser should tolerate unrelated existing console diagnostics and locate complete JSON lines; this command does not create a dedicated exclusive console protocol.

The response buffer is 192 bytes including its terminating NUL, below the installed nRF TinyUSB 256-byte TX FIFO. The foreground service waits until the complete line fits in availableForWrite, then performs one write. No heap allocation or persistent identity-response buffer is used. A response is requested only by the command; no unsolicited identity is emitted at boot. A disconnected host can still receive an incomplete physical USB transfer; discard incomplete lines and request again after reconnecting.

No identity JSON is emitted while recording, exporting, resetting, flash diagnostics, RAM observation/compression capture or GPS diagnostic ownership is active. The existing TCB command guard may queue its usual busy-refusal diagnostic outside binary export; otherwise the identity handler silently consumes busy requests. Retained compressor input/output also blocks identity. Clients must query while idle and retry after ownership has ended. A pending request is cancelled on ownership changes, disconnect, any different command, or a two-second deadline. Repeated identical requests coalesce without extending that deadline. This prevents delayed identity text entering an export. Clients must consume the solicited reply completely before starting a download; already queued response bytes are not purged. Existing console ownership and diagnostic guards are retained. Neither reading FICR nor formatting identity writes flash.

USB VID/PID, product/manufacturer/serial descriptors, application USB serial generation, UF2, DFU and bootloader files are untouched. This command exposes the canonical factory ID independently of the USB descriptor's potentially different serial format. Do not use 1200-baud touch for discovery: it can invoke the existing bootloader behaviour.

## Automated validation

`python tests/device_identity/run_tests.py` runs the real helper, console, BLE adapter, TPX3 creation and legacy recorder/export code on the existing ARM/Unicorn host models. It covers:

- Two simulated FICR devices, intentional suffix collisions, leading zeroes, zero/max uint64 and exact decimal above JavaScript's safe integer limit.
- Bounded JSON, absent hardware identity, full DIS serial and protocol 2.0.
- Idle-only guards, buffer backpressure, timeout/wrap, cancellation and disconnect.
- Full UID in newly created/decoded TPX3 metadata and unchanged accelerometer/gyro metadata.
- Byte-identical legacy CSV and existing three-session TPX3 export when DEVICE INFO is requested during transfers; no flash changes during export.

The suite is included in `python tests/reusable/run_all_tests.py`. All tests are host simulations, not radio/USB timing qualification. Build all six platformio.ini environments with `pio run`; never add an upload target for this validation.

## First-device hardware procedure (not performed)

1. Save existing recordings and note the current USB descriptor serial, COM port and full BLE DIS serial using the existing firmware. Close other console/download clients. Keep the established UF2 recovery procedure available. Obtain approval before flashing; no device has been flashed by this work.
2. On one test pod, install the intended build through the established procedure. At 115200 baud while idle, send DEVICE INFO and verify a complete v1 JSON line, exact decimal/hex agreement and repeatability across reset and power cycle. Confirm USB descriptor identity and COM-port association are unchanged.
3. Enter BLE mode through the existing controls. Verify the new display name in an active scan, full DIS serial equality with CDC, unchanged address/type, protocol 2.0 and app chooser discovery. Test existing session list/download, cancellation, disconnect and 180-second advertising timeout.
4. Capture a short test session using the existing controls. Verify decoded TPX3 device ID equals CDC decimal and DIS hex, and existing sensor metadata/recording/export remain correct. Confirm DEVICE INFO during capture/export produces no identity text, stalls or recording loss. Repeat with host backpressure and queued commands.
5. Check binary exports byte-for-byte, CRCs, legacy CSV, reconnect and normal recovery/DFU operation. Perform bootloader-mode descriptor checks separately; this work does not assume bootloader and application descriptors match.
6. Only after the first pod passes, authorise a second-pod test using the identical compiled image. Verify distinct full IDs, concurrent host device discovery and matching of each recording to the correct pod. Use full IDs for future calibration association; no shortened display name or OS/browser-local device identifier is an acceptable substitute.

Remaining qualification: real BLE advertising, actual CDC FIFO scheduling/disconnect timing, host chooser caching and multiple physical pods. This phase does not add an app identity/calibration UI, manufacturing product-serial registry or cryptographic proof of identity.

## Build resource measurements

PlatformIO release builds, installed Seeed 1.1.1 / ARM GCC 7.2.1 toolchain. Flash is linked allocated ROM plus data initialisers; RAM is static .data + .bss, as reported by PlatformIO. These figures exclude whole-runtime stack/heap peak qualification. The identity reply uses a bounded stack buffer; no persistent 192-byte buffer is added.

| Environment | Flash bytes (change) | Static RAM bytes (change) |
|---|---:|---:|
| seeed_xiao_nrf52840_sense | 167,964 (+1,744) | 119,000 (+4) |
| tcb3_first_write | 198,808 (+1,688) | 121,620 (+8) |
| tcb3_append_30s | 200,544 (+1,688) | 121,620 (+8) |
| tcb3_movement_120s | 200,584 (+1,704) | 121,620 (+8) |
| tacticpod_reusable_usb | 220,456 (+1,608) | 123,436 (+0) |
| tacticpod_reusable_ble | 333,252 (+2,432) | 144,664 (+8) |

The normal-build 119,000-byte static RAM regression limit is retained; console flags share one byte to stay within it. This leaves no margin under that test budget. Physical interrupt/runtime stack and heap peaks still require hardware qualification.

## Phase 1 validation result

All six firmware environments built successfully. The full 46-suite host regression run completed, with an initial static-RAM budget failure corrected by packing console flags; the resource check and affected early GPS/recording/console/integration suites were rerun successfully. Final combined result: 46/46 suites passing, no remaining failures. The new identity suite also passed separately against the final implementation. The original 119,000-byte RAM limit was not relaxed. `git diff --check` and whitespace checks of new files passed. Build and test artifacts are ignored under `.pio/`, including `identity-validation-results.json`, `identity-final-sizes.json` and the per-suite logs. No physical hardware was accessed or flashed.
