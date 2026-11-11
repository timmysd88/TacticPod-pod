# BLE session download protocol

Status: implemented in the opt-in `tacticpod_reusable_ble` build; target build and host tests pass. No device was flashed or physically tested. The default and reusable USB environments, TacticPod-app, production data and recordings are unchanged.

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

The notify-only Data characteristic must use Bluefruit
`setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS)`: Bluefruit copies the read
security mode to the CCCD's write permission. This permits notification
subscription without adding the Read or Write characteristic properties.
The original Phase 2 configuration used `SECMODE_NO_ACCESS` for the first
argument, creating a non-writable CCCD that violates the SoftDevice's
characteristic-add constraints. The hardware reported the aggregate
`BLE ERROR: GATT characteristic initialization`; the host regression reproduces
the Data failure with `NRF_ERROR_INVALID_PARAM` (7) using linked Bluefruit code
and a model of that documented constraint, not the proprietary SoftDevice.
Initialization now reports the characteristic name and returned error code
before the existing aggregate error/reset path. UUIDs and protocol bytes are
unchanged. The fix has not yet been uploaded or physically verified.

| Characteristic | Properties | Bluefruit read/write security | Length |
|---|---|---|---|
| Activity | Write | No access / Open | Variable, maximum 20 |
| Battery | Read | Open / No access | Fixed 6 |
| Control | Write | No access / Open | Variable, maximum 16 |
| Data | Notify | Open / No access | Variable, maximum 244; writable CCCD |
| Protocol version | Read | Open / No access | Fixed 2 |

Initialization order is Device Information Service, custom service, then
Activity, Battery, Control, Data and Protocol version. Phase 1 had only Activity
and Battery under the same custom service. All six custom UUIDs share one
vendor-specific base (only the SoftDevice's 16-bit field varies); the installed
Bluefruit default provides ten base slots. The nRF52840 attribute-table default
is 4096 bytes. No UUID-slot or attribute-table increase is needed to correct
the invalid CCCD. `configPrphConn()` controls connection MTU/event/queue
resources, not attribute permissions. The successful stack/service setup on
hardware does not validate every later allocation; the host model does not
measure SoftDevice RAM or attribute-table consumption.

The The peripheral permits ATT MTU up to 247 while retaining the minimum connection-event length and one-entry notification/write-command queues. Actual MTU is negotiated by the browser/OS and can remain 23; every notification is sized to the negotiated MTU. The opt-in target build used 140,992 / 237,568 B RAM (59.3%) and 316,612 / 811,008 B application flash (39.0%). This is a link-time measurement only; runtime heap, radio throughput and physical power/timing have not been measured.

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

`GET` uses the existing `Tcb3::Export::beginSession()` path. The TPX3 stream consists of the existing BEGIN packet, address-tagged flash-page packets, and END packet; no download-only recording format is introduced. Every TPX3 packet has its existing CRC32. Reconstructed TCB3 pages, blocks, metadata and footer are validated by the existing decoder. There is no aggregate transfer SHA-256 in protocol v2; clients must verify each BLE frame CRC, TPX3 packet CRC, and reconstructed TCB3 contents before importing.

There is no resume offset. After a disconnect, restart with `LIST` and request the session again from its BEGIN packet. This is safe because the flash is read-only and transfer identity is `(device serial, resetEpoch, sessionId)`. After all requested sessions are validated/imported, the app should disconnect explicitly; firmware does not disconnect automatically on `TRANSFER_END`. A healthy transfer refreshes the BLE-mode progress timer through ACKs. A stalled transfer is aborted after 60 seconds without acknowledged progress, while the five-minute connection inactivity timer remains suspended only during active transfer.

## App integration and validation

The app should connect to the advertised service, read protocol version, subscribe to `0005`, send `LIST`, assemble/ACK every notification fragment in order, and validate the returned manifest. For each eligible session, send `GET`, concatenate the raw TPX3 packet payloads in sequence, verify the existing TPX3 decoder result, deduplicate using device serial + reset epoch + session ID, and then import through the existing TPX3 path. Send `CANCEL` to stop an active transfer. Phase 2 firmware support does not itself implement the website workflow or automatic import.

Host regression: `py -3.13 tests\reusable\manifest_tests.py` tests the known physical three-session fixture, flash immutability, corruption rejection, incomplete-footer reporting, reset epochs, targeted TPX3 export, missing/corrupt transport packets and deterministic restart. `py -3.13 tests\reusable\ble_protocol_tests.py` tests CRC and framing at MTU 23 and 247. `py -3.13 tests\reusable\ble_phase1_tests.py` covers lifecycle, progress timeout and main-loop isolation. `py -3.13 -m platformio run -e tacticpod_reusable_ble` builds the opt-in target. None of these tests establish real browser interoperability, negotiated MTU, RF reliability, transfer throughput or hardware timing; those require an approved physical test.

## Risks before physical testing

- The current TacticPod-app Phase 1 client is connect-only (`sessionDownload: false`); it cannot perform this protocol. Use a separate protocol-v2 BLE test client for the first physical download. TPX3 compatibility was checked by decoding firmware-exported bytes with the app's existing TPX3 decoder, but this does not verify its BLE integration.
- No on-device BLE transfer has been exercised. Verify service discovery, notification subscription, command writes, ACK/retry behavior at the negotiated MTU (including MTU 23), transfer completion, disconnect, and reconnect on the intended browser/OS.
- The client must reject incomplete transfers and validate every frame CRC, TPX3 packet CRC, and reconstructed TCB3 contents before import. Protocol v2 has neither an aggregate SHA-256 nor resume; after connection loss, restart with LIST and download the session again.
- The manifest scanner and transfer exporter are read-only by construction; host tests compare flash bytes before and after scanning/export. Physical flash behavior and preservation under reset, disconnect, and power loss remain unverified. Do not include reset/erase operations in a download test.
- Link-time use is 140,992 / 237,568 B RAM (59.3%) and 316,612 / 811,008 B application flash (39.0%). This leaves 96,576 B within the reported RAM region, but runtime heap/stack high-water, BLE-stack allocation, RF throughput, and power/timing margins have not been measured on hardware.
