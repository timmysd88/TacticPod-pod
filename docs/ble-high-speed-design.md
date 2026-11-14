# Optional high-speed BLE transfer: design proposal

Status: design only. No high-speed frames, negotiation, ACKs or retransmission
are implemented. Protocol 2 LIST/GET, frame CRC16, per-fragment ACK and
stop-and-wait behavior remain unchanged.

## Compatibility and negotiation

Keep the existing protocol-version characteristic at `[2, 0]` and the existing
Control/Data properties and v2 operations. A future firmware exposes an
additional, optional, read-only capability characteristic and a separate ACK
characteristic; UUIDs and the capability layout must be allocated in the
implementation change, not inferred from the diagnostics UUIDs.

The app discovers the optional capability after connecting and before a GET.
It advertises a high-speed mode version, supported windows (4/8/16), maximum
frame size and ACK transport properties. Absence, unreadable/unsupported
version or no mutually supported window means use v2. An optional SET_MODE
command on existing Control selects the mode and window with a nonzero transfer
ID. The firmware must explicitly confirm the selection using an acknowledged
v2 response before any high-speed frame is sent. An error, timeout, link loss,
or ambiguous confirmation never permits an assumed mode: cancel or disconnect
as needed and reconnect using v2. New firmware starts in v2 every connection;
old apps never attempt selection.

Only GET uses the new mode; LIST and discovery retain v2. A GET still requires
the current connection's validated LIST selection `(sessionId, resetEpoch)`.
There is one transfer at a time; the browser allocates a fresh nonzero v2
transfer ID and the pod binds the selected mode and session identity to it.
Do not silently reuse a transfer ID while any old frames/ACKs can be queued;
disconnect/reconnect before ID wrap if necessary. Neither mode resumes after
disconnect. No recording, reset, erase or USB export may overlap BLE ownership.

## Frame stream and integrity

Proposed high-speed Data notification, little-endian:

| Offset | Field |
|---:|---|
| 0 | mode version `u8 = 3` (not the v2 protocol-version characteristic) |
| 1 | flags `u8`: bit 0 = final stream frame; other bits zero |
| 2 | transfer ID `u16` |
| 4 | frame sequence `u32`, starting at zero, no wrap |
| 8 | payload length `u8` |
| 9 | consecutive TPX3 stream bytes |
| 9 + length | CRC16/CCITT-FALSE over header and payload |

Frame length is at most `min(244, negotiated MTU - 3)`; overhead is 11
bytes, leaving 9 payload bytes at MTU 23 or 233 at MTU 247. The final flag
marks the last bytes of the stream, not success by itself. A future mode may
instead elect to refuse small MTUs and fall back to v2, but must never assume
247. The sequence bound is safely below `2^32` for the existing maximum
recording, including at the minimum payload; fail before sequence overflow.

The byte stream is the unchanged TPX3 BEGIN, PAGE and END packets, crossing
packet boundaries where useful. The browser incrementally reconstructs packets,
checks TPX3 sequence and CRC32, requires exactly one valid END and checks
session/epoch/device identity and total length against the selected manifest.
For the current session exporter the expected total is
`sectorCount * 16 * 280 + 40 + 24` bytes; validate bounds and arithmetic.
The existing strict decoder must still verify TCB3 page, block and footer CRCs,
counts and completion before import. Reject ISSUE, malformed lengths, trailing
bytes, premature/missing final flag, missing END, wrong identity and corruption.
Frame ACKs mean bounded receiver acceptance, **not** successful import.
After the final stream frame is cumulatively acknowledged, the pod sends an
acknowledged v2 `TRANSFER_END` containing session, epoch and total byte count.
The app must continue accepting and ACKing identical retransmitted final frames
and `TRANSFER_END` until the completion handshake is resolved; lost final ACKs
must not turn a valid transfer into silent completion or corrupt the next
operation. The mode capability must include a readable status that identifies
the transfer and reports `COMPLETED` only after foreground processing of the
final ACK. The app polls this status with bounded retries, re-ACKs duplicate
end messages, and does not reuse the channel until it sees `COMPLETED`. On
status timeout or disconnect it reports failure and closes the connection.

## Window and recovery

Negotiate W=4 initially; measure W=8 and W=16, all independently bounded. The
sender may submit only sequence numbers in `[base, base + W)`; `base` is the
receiver's next contiguous sequence. Queue ownership is separate from the
application window: a successful SoftDevice submission does not acknowledge
delivery. Store each complete frame and metadata until acknowledged; on
queue-full, retain it and wait for TX-complete/credit without advancing it.
Bluefruit's current `notify` can block waiting for its semaphore; any nonblocking
submission implementation must coordinate with that semaphore and SoftDevice
credits. Bound work per foreground pass and preserve lifecycle progress.

Proposed ACK written on the new characteristic:

| Offset | Field |
|---:|---|
| 0, 1 | mode `u8=3`, opcode `u8=1` |
| 2 | transfer ID `u16` |
| 4 | next contiguous frame sequence `u32` |
| 8 | received bitmap `u16`; bit i covers `next + i` |
| 10 | CRC16 over the preceding 10 bytes |

All frames below `next` were accepted contiguously; bitmap bits identify
additional accepted frames within the negotiated window. Bit 0 must be zero
because `next` is the first missing sequence. The receiver must retain all
bitmap-acknowledged bytes until they are delivered in order. It sends a
cumulative ACK when the window fills, on a gap, and after a bounded ACK timer;
it repeats its current ACK after duplicate frames or ACK loss. The sender
validates identity, CRC, monotonic bounds, sent ranges and bitmap before
releasing storage. Duplicate and stale ACKs are harmless; impossible ACKs
fail closed. On timeout it selectively retransmits only missing, previously
submitted frames. Bounded retries and a progress timeout abort rather than
report success. Timeout values and ACK cadence require measured link data.

Write-with-response is the interoperable ACK baseline. If supported by both
the characteristic and browser, write-without-response can be negotiated as
an option; promise completion is not pod receipt. Repeated cumulative ACKs
recover lost writes, and the pod needs bounded ACK coalescing/queueing rather
than the current single-slot Control callback. Explicitly test slow browser
dispatch, ACK queue overflow, disconnect, cancel and retransmission races.

## Memory and scheduling

Retaining W complete 244-byte frames costs 976, 1,952 or 3,904 bytes for
windows 4, 8 or 16, plus metadata, packet producer state, alignment, stack,
and separately configured SoftDevice queues. A design storing source packets
instead must prove their immutable lifetime and account for all queued packets.
The existing exporter holds one 280-byte packet; extending across packet
boundaries requires bounded producer state and must not overwrite data needed
for recovery. No unbounded queue or blocking flash transaction is permitted.
Flash reads and packet CRC remain synchronous foreground work unless a later
measured prefetch stage introduces a bounded second buffer.

Browser receive/reorder storage is at most W frames plus metadata and the
existing manifest-bounded TPX3 session buffer. Reject lengths beyond that
buffer before allocation or append. Account for the existing strict decoder's
additional copies and worker memory; test maximum supported session, allocation
failure and cleanup. Do not retain both unlimited received frames and a second
unbounded session copy.

## Implementation and validation gates

1. Measure actual link and ACK timing using the optional diagnostics; hardware
   access and any application-only upload need separate approval.
2. Independently test link negotiation/resource changes with MTU 23 and 247,
   1M/2M and rejected requests; preserve v2 fallback and RAM/stack margins.
3. Implement capability negotiation and v2 fallback without streaming changes.
4. Implement window 4 with cumulative ACK and selective recovery, then test
   8/16 only after verifying memory and queue behavior. Inject frame and ACK
   loss, corruption, reordering, duplicates, stale IDs, bad CRCs, timeouts,
   full queues, disconnects, cancellation and reset. Compare exported bytes and
   strict decoder results with v2; prove flash immutability and USB/recording
   exclusion.
5. Add flash prefetch only if measured producer stalls remain; improve
   discovery separately without weakening any validation.

Windows Web Bluetooth has browser/adapter-dependent MTU, DLE, PHY, interval,
notification delivery and write-without-response behavior. A larger configured
MTU is not evidence of negotiation; Web Bluetooth provides no portable control
over these parameters. Measure on each supported combination. GATT caching,
browser background throttling and simultaneous operations can disrupt
negotiation or recovery; the app must fail closed or fall back explicitly.
No throughput target is guaranteed until real hardware tests pass.
