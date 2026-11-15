# Optional high-speed BLE transfer: Phase 2

Status: the four-frame sender and browser receiver are integrated with GATT
and GET. The BLE build intentionally enables optional capability discovery;
streaming requires explicit selection and confirmation. Existing clients retain
protocol 2 LIST/GET, CRC16, per-fragment ACK and stop-and-wait behavior.
Host regressions do not qualify the physical radio, throughput or runtime RAM
margin. No firmware upload or physical benchmark has been performed.

## Compatibility and negotiation

Keep the existing protocol-version characteristic at `[2, 0]` and the existing
Control/Data properties and v2 operations. The optional read-only capability uses
`b9730009-7d2a-4f91-a380-8b52c029dc61`; write-with-response ACKs use
`b973000a-7d2a-4f91-a380-8b52c029dc61`. TPD1 and TPL1 are unchanged.

The 16-byte little-endian capability/status value is:

| Offset | Field |
|---:|---|
| 0 | ASCII `TPH1` |
| 4 | stream version `u8 = 3` |
| 5 | supported window `u8 = 4` |
| 6 | state: 0 idle, 1 selecting, 2 ready, 3 transferring/completing, 4 completed, 5 aborted |
| 7 | reserved zero |
| 8 | selected transfer ID `u16` |
| 10 | maximum notification value `u16 = 244`, not negotiated MTU |
| 12 | reserved `u16 = 0` |
| 14 | CRC16 over bytes 0 through 13 |

The app discovers the optional capability after connecting and before a GET.
Only window 4 and write-with-response are implemented; windows 8/16 and
write-without-response remain future options. Missing characteristics or a
malformed/unsupported capability mean use v2 before selection. Unexpected GATT
read errors surface rather than assuming a safe fallback. SET_MODE on Control
is a CRC-protected v2 command with opcode `0x05`, a fresh nonzero transfer ID
and payload `[3, 4]`. The pod confirms with acknowledged v2 response `0x87`
and payload `[3, 4]`; the app also requires status READY for the same ID.
GET then reuses that ID and the existing session/epoch fields. An error, timeout, link loss,
or ambiguous confirmation never permits an assumed mode: cancel or disconnect
as needed. An explicit refusal may fall back on the same link only after status
confirms idle, completed or aborted, before any high-speed GET. New firmware starts in v2 every connection;
old apps never attempt selection.

Only GET uses the new mode; LIST and discovery retain v2. A GET still requires
the current connection's validated LIST selection `(sessionId, resetEpoch)`.
There is one transfer at a time; the browser allocates a fresh nonzero v2
transfer ID and the pod binds the selected mode and session identity to it.
Do not silently reuse a transfer ID while any old frames/ACKs can be queued;
disconnect/reconnect before ID wrap if necessary. Neither mode resumes after
disconnect. No recording, reset, erase or USB export may overlap BLE ownership.

High-speed browser cancellation deliberately closes the connection at selection,
during streaming or while final completion is pending. It does not wait behind
a potentially stalled GATT write to send CANCEL. Pending GATT consumers reject
with the original cancellation/disconnection error; queued work and ACK timers
cannot act on a successor operation. BLE v2 retains its existing acknowledged
CANCEL behavior and connection reuse.

The peripheral disconnect callback only reports an event. State 3 and exporter
ownership may remain until the next foreground lifecycle pass; they are not
completion. That pass aborts, clears the bounded ACK queue, stops BLE and then
resets. `bleCancel()` clears download/list ownership and sleeps flash before reset.
No download resumes across disconnect. An explicit firmware CANCEL also stops
the window and releases the exporter before its v2 CANCELLED confirmation.

## Frame stream and integrity

Implemented high-speed Data notification, little-endian:

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

Implemented ACK written on the new characteristic:

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
recover lost writes. The implemented pod has a four-entry SPSC ACK queue;
overflow fails closed and foreground work consumes at most one ACK per pass.
The browser coalesces four contiguous fragments or sends after 30 ms, and
immediately ACKs gaps, duplicates, CRC failures and the final frame.
Explicitly test slow browser
dispatch, ACK queue overflow, disconnect, cancel and retransmission races.

## Memory and scheduling

Retaining W complete 244-byte frames costs 976, 1,952 or 3,904 bytes for
windows 4, 8 or 16, plus metadata, packet producer state, alignment, stack,
and separately configured SoftDevice queues. A design storing source packets
instead must prove their immutable lifetime and account for all queued packets.
The implemented producer additionally holds at most 233 bytes between packets,
and the ACK queue holds four 12-byte entries. The existing exporter holds one
280-byte packet; extending across packet
boundaries requires bounded producer state and must not overwrite data needed
for recovery. No unbounded queue or blocking flash transaction is permitted.
Flash reads and packet CRC remain synchronous foreground work unless a later
measured prefetch stage introduces a bounded second buffer.

Browser reorder storage is at most W frames, plus four recent frames retained
for duplicate-content checks, metadata and transient contiguous payload copies.
The notification chain is capped at 32 pending high-speed notifications
(at most 7,808 frame bytes for valid 244-byte values), separately from the
manifest-bounded TPX3 session buffer. Reject lengths beyond that
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

## Current validation and release requirements

Browser simulations compare complete fixture bytes and strict TPX3/TCB3 decoder
results for normal transfers, missing/corrupt/reordered/duplicate fragments,
lost data ACKs and lost final completion ACKs. Cancellation covers selection,
an outstanding four-fragment window and a stalled final ACK. Disconnect tests
await modeled peripheral foreground cleanup, verify exporter release and test a
fresh download. Late GATT work is rejected without touching the next operation.

The real firmware adapter is exercised with mocked GATT acceptance and ACKs.
Lifecycle tests drive the actual disconnect callback/controller through abort
and reset with a queued invalid ACK, including READY, an outstanding window
and pending TRANSFER_END. Explicit CANCEL covers the same stages. Session
integration tests independently verify sleeping flash, released read ownership,
repeated GET and byte-identical read-only fixture export. Window tests cover
MTU 23/247, impossible and CRC-invalid ACKs, retry limits, queue-full and queue
overflow. These are not a real firmware-to-browser radio interoperability test.

Before release, separately authorize a hardware benchmark: use a preserved
recording, capture TPD1/TPL1 and browser metrics, verify actual MTU/PHY/data
length/interval, compare high-speed and v2 output bytes with strict decoding,
and measure ACK latency, retries, flash-read time, throughput and runtime
stack/heap margin. Repeat cancellation/disconnect near selection, mid-stream
and completion, then reconnect and confirm recordings remain unchanged.
Test supported Windows Chrome/Edge adapters and browser background throttling.
