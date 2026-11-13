# BLE session download protocol

Status: implemented in the opt-in `tacticpod_reusable_ble` build. On 2026-11-11 the corrected diagnostic firmware (`37d4510`) was physically confirmed to boot, reach healthy recorder/TCB idle status, activate Bluetooth by double-tap, and connect through nRF Connect. This validates startup/connection, not the performance instrumentation described below. The GATT correction is also included in this Phase 2 build; temporary boot checkpoints are not. The default and reusable USB environments, production data and recordings are unchanged.

## GATT

The device advertises as `TacticPod` for 180 seconds after the existing double-tap gesture. The custom service is `b9730001-7d2a-4f91-a380-8b52c029dc61`.

| UUID suffix | Characteristic | Access and value |
|---|---|---|
| `0002` | Activity | Write one byte `0x01` to record valid application activity. |
| `0003` | Battery snapshot | Read 6 bytes; unchanged Phase 1 layout. |
| `0004` | Control | Write with response; versioned command packets below. |
| `0005` | Data | Subscribe to notifications before sending a list/download command. |
| `0006` | Protocol version | Read two bytes: `[2, 0]`. |
| `0007` | Optional performance snapshot | Read-only, 176 bytes; independent version 1 layout below. |

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
constraint, then accepts the five corrected transfer characteristics and optional
performance characteristic. It checks UUIDs,
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
| Performance snapshot | Read, authorized in foreground | Open / No access | Fixed 176 |

Initialization order remains Device Information Service, custom service, then
Activity, Battery, Control, Data and Protocol version, followed by the optional
Performance snapshot. All seven custom UUIDs share one vendor-specific base.
Existing UUIDs, packet formats, connection configuration, button timing and
recording behavior are unchanged. Failure to add the optional characteristic is
reported on serial and does not fail the existing transfer service. The production branch does
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

## Optional performance diagnostics

These are measurements, not throughput optimizations. Protocol 2, CRC checking,
ACK/retry decisions, cache validation, recording, flash writes/erases and the USB
16 MHz read/pump paths are unchanged. No per-fragment serial output, persistent
telemetry, reset command or extra flash reads are added.

The optional `0007` characteristic returns a coherent snapshot on demand.
Read authorization is queued to foreground service; offset zero captures the
176-byte value and subsequent ATT long-read offsets reuse that frozen value,
including at MTU 23. A new offset-zero read refreshes it. Read only while the
client is idle, after LIST/GET/ACK operations have finished; there is no periodic
publication delay or diagnostic notification stream.

Counters occupy fixed RAM and are per boot. BLE exit/disconnect resets the MCU,
so capture them **before disconnecting**. Cache-specific fields reset when the
existing manifest is invalidated. Additive counters saturate instead of wrapping;
offset 172 reports saturation. Short duration measurements use unsigned clock
differences, including across a clock rollover.

All fields are little-endian; `u64` counters should remain BigInt in JavaScript.

| Offset | Type | Meaning |
|---:|---|---|
| 0 | 4 bytes | ASCII `TPD1` |
| 4, 6 | u16 each | Snapshot version 1, total size 176 |
| 8 | u32 | Flags: connected 1, scanning 2, ready 4, failed 8, any LIST_BEGIN 16 |
| 12 | u16 | Actual connection ATT MTU; 0 means unobserved |
| 14, 16 | u16 each | Notification value capacity `min(244, MTU-3)`, fragment payload capacity minus 15-byte overhead |
| 18 | u16 | Last successfully submitted fragment payload bytes |
| 20 | u32 | Elapsed ms since first foreground observation of this connection |
| 24 | u32 | Current/latest cache wall time ms, including scheduling/standby pauses |
| 28, 32 | u32 each | Validator steps; cache state: cold 0, scanning 1, ready 2, failed 3 |
| 36 | u32 | Accepted LIST command count, including ERROR-7 retries |
| 40 | u32 | Latest LIST receipt to first LIST_BEGIN notification submission, us |
| 44 | u32 | First LIST since previous LIST_BEGIN to LIST_BEGIN, ms, including ERROR-7 retries |
| 48, 52 | u32 each | Accepted notification fragments; matching ACKs |
| 56, 60 | u32 each | Accepted retransmissions; rejected notification submissions |
| 64, 68 | u32 each | ACK timeouts; valid duplicate ACKs |
| 72, 76 | u32 each | Unambiguous ACK RTT samples; ACKs after retransmission |
| 80 | u64 | ACK RTT total us, excluding ACKs after retransmission |
| 88, 92 | u32 each | ACK RTT minimum/maximum us |
| 96, 104 | u64, u32 | Notification-call total/maximum us, including queue blocking and failed calls |
| 108, 112 | u32, u64 | Flash data-read calls and bytes |
| 120, 128 | u64, u32 | Flash data-read total/maximum us |
| 132 | u64 | Unique acknowledged message-payload bytes, excluding BLE headers |
| 140 | u64 | Accepted notification-value bytes, including headers and retransmissions |
| 148 | u32 | Connection observation to first LIST_BEGIN submission, ms |
| 152 | u64 | Validator-step service total us, including data I/O |
| 160 | u64 | Cache flash data-read us; subset of offset 152 |
| 168, 172 | u32 each | Snapshot uptime ms; saturation indicator |

LIST receipt is timestamped in the control callback, before foreground parsing;
LIST_BEGIN ends at successful stack submission, not over-air delivery. A
LIST_BEGIN retry does not overwrite that measurement. Fields 40/44/148 are valid
after flag 16 is set. Connection observation is not the exact radio connect
event. MTU/capacities are taken from the actual connection; received frame size
alone never establishes negotiated MTU.

ACK RTT starts when the accepted notification call returns and ends when its
matching ACK is processed in foreground. It includes radio/browser/ACK-write
and foreground scheduling delay; it is not pure radio airtime. Retransmitted
frames cannot identify which send produced an ACK, so those ACKs are counted
separately and excluded from RTT. Accepted fragments include retransmissions,
while acknowledged payload bytes count each accepted matching ACK once.

Cache wall time starts with the first validator batch after identity admission,
and freezes on completion/failure. Flash timing encloses existing data-read
transactions, including command/address/CS overhead, during cache steps or BLE
export only. USB exports and ordinary recording are excluded. Status/JEDEC/power
commands are not classified as data reads. Validator-step time includes its
data I/O but excludes between-step scheduling and batch wake/sleep; subtracting
cache read time leaves non-data-read work, not an isolated CRC benchmark.

### Measuring on the physical pod

No physical measurements or upload are part of this instrumentation change.
After separate authorization to install the application-only diagnostic image
and run the matching app locally (no production deployment):

1. Recheck device identity, stopped/healthy recorders, verified recording backups
   and a known-working rollback image. Never erase/reset recording storage.
2. Double-tap, connect, and retain browser console output for the initial LIST.
   Record whether it actually encountered a cold cache; idle warm-up may already
   have validated it. Do not start a recording/reset merely to force a cold run.
3. While idle, capture the browser report and firmware snapshot. Refresh LIST
   on the **same connection**, capture another pair, then capture a pre-download
   snapshot and download one small known session.
4. After the app finishes validation/import and is idle, capture again:

   ```javascript
   const client = window.tacticPodBluetooth.client;
   console.log(client.performanceDiagnostics);
   console.log(await client.readPerformanceDiagnostics());
   ```

5. Save each report immediately: browser list/download reports describe the
   latest operation, while firmware counters accumulate. Use pre/post-download
   deltas for flash read, notification-call, ACK, retry and byte counts. Divide
   payload-byte delta by elapsed snapshot uptime for a cross-check of useful
   bytes/s; do not confuse this with notification bytes or flash bytes.
6. Compare cold versus warm LIST, confirmed MTU, ACK delays/failures and flash
   versus notification-call time. Browser download elapsed/speed includes its
   client-side work; notification-to-ACK-write timing includes queued browser
   handling and the write-with-response completion. These measurements do not
   directly isolate RF airtime.
7. Verify the known recording still decodes with existing CRC checks, save the
   reports **before normal disconnect**, and verify stored sessions remain
   accessible. No settings, recording or flash mutation is needed.

Focused regressions are `ble_transport_tests.py` (MTU 23/247, timing, retries,
duplicates, coherent long reads, invalid reads, rollover and saturation),
`ble_cache_tests.py` (production validation timing, warm reuse, invalidation and
USB exclusion), and `ble_gatt_tests.py` (linked permissions/lengths). The existing
`usb_pump_tests.py` and `ble_phase1_tests.py` check unchanged byte-identical USB
exports, bounded scheduling, lifecycle/ownership and modeled flash immutability.
These are simulated timing/stack checks, not physical throughput measurements.

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

After a valid ACK, the next fragment is eligible on the next foreground service call, without a fixed inter-fragment delay. Each call attempts at most one notification; only a successful BLE stack submission starts the ACK timeout. A rejected submission retains the identical frame and waits 100 ms before retrying, with at most five consecutive submission failures.

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

Error codes: `1` bad protocol version/CRC, `2` malformed or unsupported command, `3` session not found/unavailable, `4` manifest validation failure or admission refusal, `5` notification/ACK timeout, `6` export failure, `7` manifest cache validation pending. Invalid or out-of-order ACKs are ignored and do not reset connection activity. A second LIST/GET while another transfer is active is ignored.

`SESSION_ENTRY` fields are: `sessionId:u32`, `resetEpoch:u32`, `firstAddress:u32`, `sectorCount:u32`, `formatVersion:u32`, `flags:u32`, `footerReason:u32`, `startUs:u64`, `stopUs:u64`, `deviceId:u64`, `committed[4]:u64`, `lost[4]:u64`. Flag bit 0 means the footer is sealed, bit 1 means stored pages/blocks and footer counts passed validation, and bit 2 means clean completion with no loss. The scanner reports at most 64 entries; overflow is explicit. Incomplete sessions are counted but not offered for download.

## Storage and reconstruction

`LIST` uses the existing 64-entry RAM manifest without rescanning flash. After boot recovery, the unchanged validator populates that same allocation incrementally: reset markers/epochs, header and continuation identity, each page CRC, block CRC/envelope/counts, and session footer/counts are all checked before the cache becomes ready. There is no second session table, persistent index or additional flash write.

An awake idle loop performs at most one validator step when acquisition slack permits. Recording/admission/finalisation, recovery, USB export, diagnostics, identity queries and GPS/capture work have priority. No cache work runs in standby. While BLE is connected and no LIST/GET owns the manifest, up to 16 steps run per service call, waking and sleeping only external flash for that batch. Unchanged flash ownership guards apply to every batch; no transaction is held between steps.

Before readiness, an otherwise admissible `LIST` immediately returns `ERROR 7` (payload `07 00`), never a successful empty inventory or a partial manifest. This previously unused code means only cold/in-progress cache validation. Background warm-up continues while awake or BLE-connected and idle. Actual validation failures, admission/ownership conflicts and serialization failures retain `ERROR 4`; validation failures remain latched until invalidation. Error framing remains the existing two-byte little-endian payload with unchanged version, UUIDs, CRCs and ACKs. Existing LIST_BEGIN status 2 remains reserved for compatibility; the cached path rejects a failed scan before LIST_BEGIN. Overflow still produces status 1 with at most 64 validated entries; incomplete sessions are counted but not offered.

Accepted TCB recording and confirmed reset invalidate the cache before mutation. Legacy recording/erase admission also invalidates if enabled; reusable builds retain their existing refusal of those legacy mutations. Completion does not synthesize entries: after recovery becomes idle, the full validator rebuilds in place. Thus interrupted sessions, changed epochs and corrupt data retain the existing fail-closed rules. Cancellation discards LIST selection but preserves a ready cache while the connection remains open. BLE-mode exit/disconnect still uses the existing MCU reset, which loses the RAM cache. GET requires a fresh LIST selection on that connection and exports the indexed sector range without copying recording data.

Warm LIST_BEGIN is eligible on the next BLE service call, with no flash reads. This is not a universal three-second guarantee: cold boot, a new recording/reset, validation failure and standby pauses can delay readiness substantially, and complete LIST/GET latency still depends on MTU, ACK latency and session count. The updated app ACKs ERROR 7, displays `Preparing saved recordings...`, waits two seconds and sends a fresh LIST request ID over the same connection. It retries only this validated response before LIST_BEGIN, for at most five minutes. ERROR 4 (including older firmware's ambiguous warming response), other errors, cancellation and disconnect stop preparation. Cancellation during a retry gap needs no CANCEL for the finished ERROR response; an overall deadline during an active LIST closes that connection safely. Normal session selection, GET and download validation remain unchanged. Older apps treat ERROR 7 as terminal, so automatic preparation requires both updates. Cache validity describes the last completed validation; it cannot detect subsequent spontaneous flash corruption without reading it. Clients must still validate downloaded page/block/footer CRCs.

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

GET and BLE-mode background cache batches check recorder/flash admission first, issue release `0xAB`,
wait the existing conservative 100 microseconds, require status zero
(valid idle chip, WIP/WEL clear), and verify JEDEC identity five times before
starting reads. GPS, IMU and the recording clocks remain suspended; this
wakes only external NOR, not the full recording path.

Cached LIST leaves flash asleep and never issues SPI commands. BLE background
validation returns flash to sleep after each bounded batch. During GET,
flash remains awake through export and the final acknowledgement, then returns
to deep-power-down with the existing 5 microsecond entry delay. GET on the same
connection can re-wake and re-verify flash without re-scanning. Cancellation, transfer errors, disconnect
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

Code 7 now identifies only pending cache validation. Code 4 retains validation
failure, admission refusal or a missing manifest entry during serialization.
A failed background scan never produces a successful LIST.
ACK timeout remains code 5; unavailable GET remains
code 3. Firmware version/UUIDs, framing, checksums and ACK behavior are unchanged.

`GET` uses the existing `Tcb3::Export::beginSession()` path. The TPX3 stream consists of the existing BEGIN packet, address-tagged flash-page packets, and END packet; no download-only recording format is introduced. Every TPX3 packet has its existing CRC32. Reconstructed TCB3 pages, blocks, metadata and footer are validated by the existing decoder. There is no aggregate transfer SHA-256 in protocol v2; clients must verify each BLE frame CRC, TPX3 packet CRC, and reconstructed TCB3 contents before importing.

There is no resume offset. After a disconnect, restart with `LIST` and request the session again from its BEGIN packet. This is safe because the flash is read-only and transfer identity is `(device serial, resetEpoch, sessionId)`. After all requested sessions are validated/imported, the app should disconnect explicitly; firmware does not disconnect automatically on `TRANSFER_END`. A healthy transfer refreshes the BLE-mode progress timer through ACKs. A stalled transfer is aborted after 60 seconds without acknowledged progress, while the five-minute connection inactivity timer remains suspended only during active transfer.

## App integration and validation

The app should connect to the advertised service, read protocol version, subscribe to `0005`, send `LIST`, assemble/ACK every notification fragment in order, and validate the returned manifest. For each eligible session, send `GET`, concatenate the raw TPX3 packet payloads in sequence, verify the existing TPX3 decoder result, deduplicate using device serial + reset epoch + session ID, and then import through the existing TPX3 path. Send `CANCEL` to stop an active transfer. Phase 2 firmware support does not itself implement the website workflow or automatic import.

Host regression: `py -3.13 tests\reusable\manifest_tests.py` tests the known physical three-session fixture, flash immutability, corruption rejection, incomplete-footer reporting, reset epochs, targeted TPX3 export, missing/corrupt transport packets and deterministic restart. `py -3.13 tests\reusable\ble_protocol_tests.py` tests CRC and framing at MTU 23 and 247. `py -3.13 tests\reusable\ble_phase1_tests.py` covers lifecycle, progress timeout and main-loop isolation. `py -3.13 -m platformio run -e tacticpod_reusable_ble` builds the opt-in target. None of these tests establish real browser interoperability, negotiated MTU, RF reliability, transfer throughput or hardware timing; those require an approved physical test.

`tests\reusable\ble_cache_tests.py` exercises production session/main/manifest code for cold/ready LIST, repeated RAM-only discovery, bounded idle work, USB/recording/diagnostic contention, standby pause/resume, completed append, confirmed reset, erase refusal/invalidation, corrupted pages, interrupted sessions and overflow. It distinguishes pending validation from failures and ownership conflicts. `ble_transport_tests.py` also checks the actual adapter's ERROR-7 pending versus ERROR-4 failure framing, ACK completion and a successful subsequent LIST retry. These use modeled NOR and do not access hardware.

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
