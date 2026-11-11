# BLE session download protocol

Status: implemented in the opt-in `tacticpod_reusable_ble` build. On 2026-11-11 the corrected diagnostic firmware (`37d4510`) was physically confirmed to boot, reach healthy recorder/TCB idle status, activate Bluetooth by double-tap, and connect through nRF Connect. This validates startup/connection, not an end-to-end session download. The GATT correction is also included in this non-diagnostic Phase 2 build; temporary boot checkpoints are not. The default and reusable USB environments, TacticPod-app, production data and recordings are unchanged.

## GATT

The device advertises as `TacticPod` for 180 seconds after the existing double-tap gesture. The custom service is `b9730001-7d2a-4f91-a380-8b52c029dc61`.

| UUID suffix | Characteristic | Access and value |
|---|---|---|
| `0002` | Activity | Write one byte `0x01` to record valid application activity. |
| `0003` | Battery snapshot | Read 6 bytes; unchanged Phase 1 layout. |
| `0004` | Control | Write with response; versioned command packets below. |
| `0005` | Data | Subscribe to notifications before sending a list/download command. |
| `0006` | Protocol version | Read two bytes: `[2, 0]`. |

Standard Device Information Service exposes manufacturer `TacticPod`, model `XIAO nRF52840 Sense`, the FICR-derived serial number, and firmware revision `ble-download-2`. GATT is open and unbonded. Existing pairing rejection, battery and activity behavior remain in place.

The notify-only Data characteristic uses Bluefruit
`setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS)`. Bluefruit copies read security
to the CCCD's write permission; the original Phase 2 `NO_ACCESS` setting
therefore created an invalid non-writable CCCD. This correction permits
notification subscription without adding Read or Write characteristic
properties. Initialization failures now report the individual characteristic
name and numeric error before the existing aggregate error/reset path.

The linked Bluefruit regression rejects the original Data metadata with error
7 (`NRF_ERROR_INVALID_PARAM`) using a model of the documented writable-CCCD
constraint, then accepts all five corrected characteristics. It checks UUIDs,
properties, security and lengths; it does not emulate proprietary SoftDevice
resource allocation or radio operation. The physical diagnostic test confirmed
that initialization now reaches advertising and connection.

| Characteristic | Properties | Bluefruit read/write security | Length |
|---|---|---|---|
| Activity | Write | No access / Open | Variable, maximum 20 |
| Battery | Read | Open / No access | Fixed 6 |
| Control | Write | No access / Open | Variable, maximum 16 |
| Data | Notify | Open / No access | Variable, maximum 244; writable CCCD |
| Protocol version | Read | Open / No access | Fixed 2 |

Initialization order remains Device Information Service, custom service, then
Activity, Battery, Control, Data and Protocol version. All six custom UUIDs
share one vendor-specific base. UUIDs, packet formats, connection configuration,
button timing and recording behavior are unchanged. The production branch does
not include the diagnostic environment, retained boot structure or `BOOT STATUS`;
named GATT errors are retained because they are bounded failure-path reporting.

Merge-preparation validation passed on the non-diagnostic branch: all six
firmware environments, all 40 regression scripts, and the separate linked
core/variant startup test. ELF checks reject a boot-diagnostic section and
preserve the existing eight-byte BLE reset marker. Application-only DFU ZIP
contents, ELF payload and CRC16 were verified without flashing. Build/test
paths honor `PLATFORMIO_BUILD_DIR` so existing firmware packages are not rebuilt
or overwritten during isolated validation.

The peripheral permits ATT MTU up to 247 while retaining the minimum connection-event length and one-entry notification/write-command queues. Actual MTU is negotiated by the browser/OS and can remain 23; every notification is sized to the negotiated MTU. The corrected non-diagnostic target build uses 140,992 / 237,568 B RAM (59.3%) and 316,892 / 811,008 B application flash (39.1%). This is a link-time measurement only; runtime heap, radio throughput and physical power/timing have not been measured.

## Framing and checksums

All integers are little-endian. Commands are written with response. A command ends with a 16-bit CRC over all preceding command bytes. The CRC is CRC-16/CCITT-FALSE: initial value `0xffff`, polynomial `0x1021`, no reflection, no final XOR.

| Command | Bytes before CRC | Total length |
|---|---|---:|
| `LIST` | `version:u8=2, opcode:u8=1, transferId:u16` | 6 |
| `GET` | `version:u8=2, opcode:u8=2, transferId:u16, sessionId:u32, resetEpoch:u32` | 14 |
| `ACK` | `version:u8=2, opcode:u8=3, transferId:u16, messageSequence:u32, offset:u16, frameCrc:u16` | 14 |
| `CANCEL` | `version:u8=2, opcode:u8=4, transferId:u16` | 6 |

The client supplies a nonzero transfer ID. Only one transfer is active at a time. `LIST`/`GET` require Data notifications to be enabled first. A `GET` must reference a session returned by `LIST` on the same connection, using both session ID and reset epoch.

Each Data notification is one fragment of a logical message:

| Offset | Field |
|---:|---|
| 0 | protocol version `u8=2` |
| 1 | message type `u8` |
| 2 | transfer ID `u16` |
| 4 | message sequence `u32` |
| 8 | fragment offset `u16` |
| 10 | total message length `u16` |
| 12 | fragment payload length `u8` |
| 13 | payload |
| `13 + payload length` | CRC-16/CCITT-FALSE `u16`, over header and payload |

The frame is at most `min(244, negotiated MTU - 3)` bytes. A message may span multiple notifications. There is one outstanding notification fragment; the client ACKs each fragment using its transfer ID, message sequence, offset and notification CRC. Duplicate ACKs for the last accepted fragment are ignored safely. Notifications are retried up to five times, with a one-second ACK timeout. A stalled or unacknowledged transfer is abandoned rather than reported complete; the connection can remain open.

Message types:

| Type | Payload |
|---|---|
| `0x81 LIST_BEGIN` | `status:u16, entryCount:u16, incompleteCount:u32`; status `0=complete`, `1=manifest overflow`, `2=scan failure`. |
| `0x82 SESSION_ENTRY` | 116 bytes, layout below. |
| `0x83 LIST_END` | `entryCount:u16`. |
| `0x84 TPX3_PACKET` | One complete existing TPX3 transport packet, fragmented only by the BLE message framing. |
| `0x85 TRANSFER_END` | `sessionId:u32, resetEpoch:u32, totalTpx3Bytes:u32`; all TPX3 packets have been ACKed. |
| `0x86 CANCELLED` | `status:u16`, currently zero. |
| `0xff ERROR` | `errorCode:u16`. |

Error codes: `1` bad protocol version/CRC, `2` malformed or unsupported command, `3` session not found/unavailable, `4` manifest failure, `5` notification/ACK timeout, `6` export failure. Invalid or out-of-order ACKs are ignored and do not reset connection activity. A second LIST/GET while another transfer is active is ignored.

`SESSION_ENTRY` fields are: `sessionId:u32`, `resetEpoch:u32`, `firstAddress:u32`, `sectorCount:u32`, `formatVersion:u32`, `flags:u32`, `footerReason:u32`, `startUs:u64`, `stopUs:u64`, `deviceId:u64`, `committed[4]:u64`, `lost[4]:u64`. Flag bit 0 means the footer is sealed, bit 1 means stored pages/blocks and footer counts passed validation, and bit 2 means clean completion with no loss. The scanner reports at most 64 entries; overflow is explicit. Incomplete sessions are counted but not offered for download.

## Storage and reconstruction

`LIST` performs a foreground, bounded, read-only scan of the TCB3 recording partition. It validates reset markers/epochs, header and continuation identity, each page CRC, block CRC/envelope/counts, and session footer/counts. It never calls flash program or erase operations. A reset marker causes stale pre-reset sessions to be hidden while current-epoch sessions at the reusable write boundary remain discoverable.

### BLE flash power lifecycle correction

The reported `LIST (0x01)` failure with `ERROR (0xff), code 4` exposed a
firmware power-state gap. `main.cpp` enters standby before advertising (or is
already in standby), issuing external flash deep-power-down `0xB9`. The BLE
loop bypasses normal standby exit. Previously `bleBeginList()` then called
JEDEC identity verification without releasing deep-power-down; an asleep
chip cannot return the required `0B 40 18` identity.
The host regression reproduces this failure through the production main
entry and session code before the correction. The physical response confirms
the pod-origin error, but its exact JEDEC/status values have not been measured.

LIST and GET now check recorder/flash admission first, issue release `0xAB`,
wait the existing conservative 100 microseconds, require status zero
(valid idle chip, WIP/WEL clear), and verify JEDEC identity five times before
starting reads. GPS, IMU and the recording clocks remain suspended; this
wakes only external NOR, not the full recording path.

Flash remains awake through scan/export and the final acknowledgement.
After LIST_END or TRANSFER_END is acknowledged, flash returns to
deep-power-down with the existing 5 microsecond entry delay. LIST keeps its
validated manifest in RAM, so GET on the same connection can re-wake and
re-verify flash without re-scanning. Cancellation, transfer errors, disconnect
and BLE-mode shutdown also release the read ownership and attempt safe
power-down. A failed idle-status check never forces sleep onto a busy chip.
The boot path still releases deep-power-down after the existing BLE-mode
reset. No write-enable, program, erase or recording-reset command is added.

Bounded, nonblocking serial diagnostics distinguish:

- `wake/status failed`: the post-release status check failed.
- `JEDEC identity failed`: includes the observed six-digit JEDEC value.
- `ownership/admission conflict` or LIST/GET ownership conflict: no wake or
  flash read is attempted when another owner holds the chip.
- Manifest errors: the scanner's existing reason, such as invalid marker,
  header/page/block CRC, continuation identity, or footer/count validation.
- `sleep refused`: cleanup found a non-idle/invalid flash status and did
  not issue deep-power-down.

Diagnostics use a fixed 256-byte queue, drain only available USB TX space,
and report suppression if that queue fills. They do not print per page/frame
or wait for a serial host. Wake failure means an invalid post-release status;
SPI cannot independently acknowledge receipt of opcode `0xAB`.

Protocol error codes are unchanged. Code 4 can also mean admission
refusal/range initialization failure, or a missing manifest entry during
serialization. A scan that starts but later fails returns LIST_BEGIN status 2,
not immediate ERROR 4. ACK timeout remains code 5; unavailable GET remains
code 3. Firmware version/UUIDs, framing, checksums and ACK behavior are unchanged.

`GET` uses the existing `Tcb3::Export::beginSession()` path. The TPX3 stream consists of the existing BEGIN packet, address-tagged flash-page packets, and END packet; no download-only recording format is introduced. Every TPX3 packet has its existing CRC32. Reconstructed TCB3 pages, blocks, metadata and footer are validated by the existing decoder. There is no aggregate transfer SHA-256 in protocol v2; clients must verify each BLE frame CRC, TPX3 packet CRC, and reconstructed TCB3 contents before importing.

There is no resume offset. After a disconnect, restart with `LIST` and request the session again from its BEGIN packet. This is safe because the flash is read-only and transfer identity is `(device serial, resetEpoch, sessionId)`. After all requested sessions are validated/imported, the app should disconnect explicitly; firmware does not disconnect automatically on `TRANSFER_END`. A healthy transfer refreshes the BLE-mode progress timer through ACKs. A stalled transfer is aborted after 60 seconds without acknowledged progress, while the five-minute connection inactivity timer remains suspended only during active transfer.

## App integration and validation

The app should connect to the advertised service, read protocol version, subscribe to `0005`, send `LIST`, assemble/ACK every notification fragment in order, and validate the returned manifest. For each eligible session, send `GET`, concatenate the raw TPX3 packet payloads in sequence, verify the existing TPX3 decoder result, deduplicate using device serial + reset epoch + session ID, and then import through the existing TPX3 path. Send `CANCEL` to stop an active transfer. Phase 2 firmware support does not itself implement the website workflow or automatic import.

Host regression: `py -3.13 tests\reusable\manifest_tests.py` tests the known physical three-session fixture, flash immutability, corruption rejection, incomplete-footer reporting, reset epochs, targeted TPX3 export, missing/corrupt transport packets and deterministic restart. `py -3.13 tests\reusable\ble_protocol_tests.py` tests CRC and framing at MTU 23 and 247. `py -3.13 tests\reusable\ble_phase1_tests.py` covers lifecycle, progress timeout and main-loop isolation. `py -3.13 -m platformio run -e tacticpod_reusable_ble` builds the opt-in target. None of these tests establish real browser interoperability, negotiated MTU, RF reliability, transfer throughput or hardware timing; those require an approved physical test.

The flash-power regressions in `ble_phase1_tests.py` additionally execute the
real LIST/GET session functions after physical-button BLE entry with a mock
adapter and NOR model. They cover sleeping flash, the full release delay,
identity/status rejection, manifest failure diagnostics, LIST/GET ownership
conflicts, repeated exports, cancellation and disconnect cleanup. All 16 MiB
of modeled NOR are compared before/after each new case (including the deliberate
malformed-input fixture); no enable/program/erase operation is accepted.
These are not radio or chip electrical tests.

Local correction validation: all six firmware environments built, all 40
regression scripts passed, and the separate linked core/variant startup check
passed. The full runner initially completed 39 scripts successfully; the GATT
script could not import Unicorn from the system Python. Re-running that script
with the already installed `.pio-core/test-python` on `PYTHONPATH` passed its
original-invalid-CCCD negative control and all five corrected characteristics.
No dependency installation or firmware upload was needed. For this local
toolchain, set `$env:PYTHONPATH=(Join-Path (Get-Location) '.pio-core\test-python')`
before invoking the full runner.

### Reset-epoch manifest scan correction

After a valid reset marker, the manifest now starts at sector one (`0x1000`),
matching recorder recovery and USB export. Previously it started at the
pre-reset reusable write boundary (`0x39000`), skipping completed recordings
or entering a continuation sector and failing with `TCB3 session prefix missing`
(LIST_BEGIN status 2). Only that marker branch changes; blank-sector-zero
checks, allocation/write addresses, protocol, CRCs and ACKs are unchanged.

`tests/reusable/manifest_status2_tests.py` preserves the investigation cases as
regressions for corrected discovery below/across `0x39000`, multiple sessions,
incomplete final sessions, legacy layouts, read/status failures and overflow.
It compares all 16 MiB of modeled flash before/after scanning and USB export,
including the reserved final sector. Optional TPX3 paths exercise archived
exports with a modeled reset marker: USB exports omit the actual marker, so
these are not raw captures of current flash. The full regression runner includes
the self-contained cases. An incomplete session immediately followed by another
session still fails closed with a continuation mismatch; this separate existing
limitation is not changed by the scan-start correction.

Correction validation: all six firmware environments built. The complete
41-script runner passed 40 scripts; the standby source-hash guard detected the
intentional store change. After updating only that reviewed source digest, the
standby script passed on rerun. Separate linked startup checks and both archived
export cases passed. BLE protocol and lifecycle tests passed without protocol
changes. No hardware commands or uploads were performed for this correction.

### Proposed application-only upload (not performed)

1. Obtain separate authorization for the exact pod and application image.
   Keep the pod idle, disconnect the browser/nRF Connect, close serial clients,
   and use stable USB power. Do not start/stop a recording, erase/reset recordings,
   or change the bootloader/SoftDevice.
2. Build the reviewed commit using
   `py -3.13 -m platformio run -e tacticpod_reusable_ble` (no upload target).
   Inspect `.pio/build/tacticpod_reusable_ble/firmware.zip`: its manifest must
   contain only an application image plus DFU format metadata, with no
   bootloader/SoftDevice payload. Verify package CRC and that the binary matches
   the ELF application region starting at `0x27000`, below `0xED000`.
   Record the ZIP's SHA-256; a rebuild can change the archive hash.
3. Only after approval, enter the existing board bootloader with its established
   double-reset procedure, positively identify its USB/COM port, and use the
   existing Adafruit application-only serial DFU path to install that verified
   ZIP. No SWD mass erase, combined image, recovery/provisioning or full-chip
   flashing is part of this procedure. Stop on any mismatch or DFU failure.
4. Confirm normal idle startup without starting a recording. Then perform only
   an explicitly authorized BLE connection and one LIST, capturing any bounded
   serial diagnostic. Stop after listing; a physical GET/download needs its own
   authorization.

## Risks before physical testing

- The updated TacticPod-app physically connects, detects protocol 2.0 and sends LIST; the reported pod ERROR 4 motivated the correction above. No physical recording download has been performed. TPX3 compatibility was checked by decoding firmware-exported bytes with the app's existing TPX3 decoder, but this does not verify a complete physical BLE import.
- No on-device recording transfer has been exercised. Re-verify LIST after a separately authorized application update, then seek separate authorization for a small completed recording download. Validate ACK/retry behavior at the negotiated MTU (including MTU 23), transfer completion, disconnect, and reconnect on the intended browser/OS.
- The client must reject incomplete transfers and validate every frame CRC, TPX3 packet CRC, and reconstructed TCB3 contents before import. Protocol v2 has neither an aggregate SHA-256 nor resume; after connection loss, restart with LIST and download the session again.
- The manifest scanner and transfer exporter are read-only by construction; host tests compare flash bytes before and after scanning/export. Physical flash behavior and preservation under reset, disconnect, and power loss remain unverified. Do not include reset/erase operations in a download test.
- With the flash-power correction, link-time use is 141,272 / 237,568 B RAM (59.5%) and 319,516 / 811,008 B application flash (39.4%). This leaves 96,296 B within the reported RAM region, but runtime heap/stack high-water, BLE-stack allocation, RF throughput, and power/timing margins have not been measured on hardware.
