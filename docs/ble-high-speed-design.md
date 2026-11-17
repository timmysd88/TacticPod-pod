# Optional high-speed BLE transfer: Phase 2

Status: bounded streaming, an eight-entry notification queue and negotiated
4/8/16-frame windows are integrated with GATT and GET. The matching browser
defaults to high speed only after explicit capability discovery;
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

The legacy TPH1 value intentionally continues advertising window 4, including
during larger-window transfers. An additional optional read-only characteristic
`b973000d-7d2a-4f91-a380-8b52c029dc61` explicitly negotiates larger windows:

| Offset | TPW1 field |
|---:|---|
| 0 | ASCII `TPW1` |
| 4 | schema `u8 = 1` |
| 5 | maximum window `u8 = 16`; windows 4, 8 and 16 supported |
| 6 | recommended cumulative ACK batch `u8 = 8` |
| 7 through 9 | reserved zero |
| 10 | CRC16 over bytes 0 through 9 |

The app discovers capabilities before GET. Missing TPH1/ACK means v2; missing
TPW1 means legacy window 4. Malformed/present-but-unreadable capabilities fail
explicitly before GET, rather than silently accepting ambiguous negotiation.
With a valid TPW1 the app selects its supported maximum window. SET_MODE on Control
is a CRC-protected v2 command with opcode `0x05`, a fresh nonzero transfer ID
and payload `[3, W]`. The pod confirms with acknowledged v2 response `0x87`
and exactly `[3, W]`; the app also requires status READY for the same ID.
GET then reuses that ID and the existing session/epoch fields. An error, timeout, link loss,
or ambiguous confirmation never permits an assumed mode: cancel or disconnect
as needed. An explicit refusal may fall back on the same link only after status
confirms idle, completed or aborted, before any high-speed GET. New firmware starts in v2 every connection;
old default-v2 apps never attempt selection and legacy opt-in apps select W=4.
Window 8/16 is refused unless TPW1 was successfully initialized.

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
marks the last bytes of the stream, not success by itself. A larger effective
MTU can enlarge newly prepared frames during a transfer; already retained frames
remain byte-identical. Frames carry their own payload lengths. An MTU reduction
below the established capacity fails explicitly rather than truncating frames.
A future mode may
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

W=4 remains the legacy default; TPW1 permits explicit W=8/16 selection. The
sender may submit only sequence numbers in `[base, base + W)`; `base` is the
receiver's next contiguous sequence. Queue ownership is separate from the
application window: a successful SoftDevice submission does not acknowledge
delivery. Store each complete frame and metadata until acknowledged; on
queue-full, retain it and wait for TX-complete/credit without advancing it.
The sole notification producer uses nonblocking `sd_ble_gatts_hvx`, not
Bluefruit's semaphore-based `notify` (which can wait 100 ms). Atomic counted
credits reserve one slot before submission and return every TX-complete count.
Resource exhaustion preserves the frame without consuming a retry attempt,
waits for TX-complete or a bounded 5 ms retry delay, and never waits synchronously.
Invalid credit accounting fails explicitly. Do not mix this producer with
Bluefruit `notify` or other notification producers without redesigning accounting.

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
cumulative ACK after the negotiated batch (eight for TPW1, four for legacy),
on a gap, and after a bounded ACK timer;
it repeats its current ACK after duplicate frames or ACK loss. The sender
validates identity, CRC, monotonic bounds, sent ranges and bitmap before
releasing storage. Duplicate and stale ACKs are harmless; impossible ACKs
fail closed. A bitmap proving a gap permits one immediate selective retry;
repeated identical ACKs do not repeatedly trigger that fast retry. One-second
timeouts selectively retry missing, submitted frames, with five attempts.
Bounded retries and a progress timeout abort rather than
report success. Timeout values and ACK cadence require measured link data.

Write-with-response is the interoperable ACK baseline. If supported by both
the characteristic and browser, write-without-response can be negotiated as
an option; promise completion is not pod receipt. Repeated cumulative ACKs
recover lost writes. The implemented pod has a four-entry SPSC ACK queue;
overflow fails closed and foreground work consumes one ACK per bounded work
iteration. The browser requests an ACK after the negotiated batch or 30 ms,
and immediately for gaps, duplicates, CRC failures and the final frame.
There is at most one ACK GATT write pending/in flight, plus one coalesced latest
ACK request; receive processing does not await its write completion. GATT
failures reject the operation. Status reads and completion still serialize with
these writes. Progress rendering is separately scheduled at most every 150 ms.
Explicitly test slow browser
dispatch, ACK queue overflow, disconnect, cancel and retransmission races.

## Memory and scheduling

Retaining W complete 244-byte frames costs 976, 1,952 or 3,904 bytes for
windows 4, 8 or 16, plus metadata, packet producer state, alignment, stack,
and separately configured SoftDevice queues. The sender reserves all 16 slots
statically, even when negotiating W=4. A design storing source packets
instead must prove their immutable lifetime and account for all queued packets.
The implemented producer additionally holds at most 233 bytes between packets,
and the ACK queue holds four 12-byte entries. The existing exporter holds one
280-byte packet; extending across packet
boundaries requires bounded producer state and must not overwrite data needed
for recovery. No unbounded queue or unbounded foreground flash work is permitted.
Flash reads and packet CRC remain synchronous foreground work unless a later
measured prefetch stage introduces a bounded second buffer.

Browser reorder storage is at most W frames, plus W recent frames retained
for duplicate-content checks and metadata. Notification values are snapshotted
once on arrival; the receive window owns that immutable snapshot and passes
payload subarray views to the manifest-bounded stream buffer.
The notification chain is capped at 32 pending high-speed notifications
(at most 7,808 frame bytes for valid 244-byte values), separately from the
manifest-bounded TPX3 session buffer. Reject lengths beyond that
buffer before allocation or append. Account for the existing strict decoder's
additional copies and worker memory; test maximum supported session, allocation
failure and cleanup. Do not retain both unlimited received frames and a second
unbounded session copy.

## Implementation and validation gates

1. Implemented scheduling: download-only 1 ms cooperative yield instead of
   10 ms; up to 64 work iterations or 2 ms per streaming burst, whichever
   comes first. Frames span TPX3 packet boundaries within the same invocation.
   Control/disconnect/ACK checks remain inside the burst; outer lifecycle,
   battery and timeout servicing continues.
2. Implemented queue: eight SoftDevice HVN entries, counted nonblocking
   submission, multiple completion handling, resource backoff and bounded stalls.
3. Implemented protocol: explicitly negotiated W=8/16, legacy W=4/v2,
   cumulative/bitmap ACKs, fast gap recovery and unchanged strict completion.
4. Implemented optional link requests using installed Bluefruit/S140 APIs:
   ATT MTU 247, automatic maximum DLE (251 where available), 2M PHY and
   15 ms interval. Initial preferred interval is 7.5-15 ms. Event-length budget
   is 12 units (15 ms); Bluefruit enables connection-event extension. Requests
   are spaced by 250 ms, up to four submission attempts per stage; rejection
   logs and retains effective settings. Acceptance is not proof of negotiation.
5. Implemented browser: capability-selected default high speed, coalesced
   nonblocking ACKs, eight-frame batch on TPW1 firmware, bounded UI updates,
   reduced copies and unchanged cancellation/identity/strict validation.
6. Flash review: retain existing BLE-only 4 MHz reads, recording writes,
   storage formats and USB timings. A 260-byte read has approximately 520 us
   wire time at 4 MHz (before software/SPI/CRC overhead), versus longer ideal
   BLE delivery time for a page. Eight queued notifications overlap synchronous
   foreground production with radio work. There is no measured flash bottleneck
   justifying another buffer or a clock change. Use existing flash-time counters
   before considering either; do not infer CPU/driver cost from wire time.

### Link-time resources and remaining release gate

The BLE-only project linker script reserves 32 KiB below `0x20008000` for
SoftDevice (8 KiB more than before); default and USB layouts are unchanged.
Application FLASH remains `0x27000..0xED000`, preserving bootloader and storage
boundaries. The installed framework is untouched. Bluefruit still checks the
actual `sd_ble_enable` RAM requirement at startup: an offline link cannot prove
the proprietary stack accepts this allocation.

The three build targets pass. BLE static RAM is 148,040 / 229,376 bytes;
application flash is 336,868 / 811,008 bytes. The eight-byte NOLOAD reset marker
is separate. `__HeapBase=0x2002C250`, `__HeapLimit=0x2003F800` leaves 79,280 bytes
for runtime allocations, including FreeRTOS tasks; the top 2,048 bytes remain
the ISR stack reserve. These are address-space margins, not measured heap/stack
high-water values. Confirm startup RAM acceptance and runtime margin physically
before treating this as release-qualified.

### Throughput bounds, not measurements

At MTU 247 a full notification carries 233 TPX3 bytes in 244 GATT value bytes
(11 bytes v3 overhead, versus 15 in v2). An eight-frame cumulative ACK adds
12 application bytes per 1,864 TPX3 bytes, excluding ATT/link overhead.
TPX3 PAGE packets still wrap 256 stored bytes in 280 bytes. CRC16/CRC32,
TPX3 packet contents and completion confirmation are unchanged.

With DLE 251, continuous full packets, minimal empty link acknowledgements and
no application ACK/connection-event gaps, ideal TPX3 envelopes are approximately
94 KB/s at 1M or 167 KB/s at 2M (decimal KB). Application ACK writes, event
budgets, radio retries, central scheduling, browser dispatch and flash/CRC work
reduce these numbers. Without negotiated DLE/MTU the envelope is much lower.
W=16 still bounds bytes in flight to 3,728 at MTU 247; host ACK latency can
therefore remain limiting even with an eight-slot radio queue. No speedup has
been measured and no particular sustained throughput is promised.

Windows Web Bluetooth has browser/adapter-dependent MTU, DLE, PHY, interval,
notification delivery and write-without-response behavior. A larger configured
MTU is not evidence of negotiation; Web Bluetooth provides no portable control
over these parameters. Measure on each supported combination. GATT caching,
browser background throttling and simultaneous operations can disrupt
negotiation or recovery; the app must fail closed or fall back explicitly.
No throughput target is guaranteed until real hardware tests pass.

## Current validation and release requirements

Software validation on the throughput feature branches: default, reusable USB
and BLE builds pass; all 46 firmware regression scripts pass. The complete app
suite reports 527 passed and eight skipped tests (16 passing files, one skipped).
GATT/ELF checks validate the linked characteristic contract, application flash
boundaries, reset marker, 32 KiB reservation and heap-address margin.
These results do not establish physical throughput or runtime allocation.

Browser simulations compare complete fixture bytes and strict TPX3/TCB3 decoder
results for normal transfers, missing/corrupt/reordered/duplicate fragments,
lost data ACKs and lost final completion ACKs. Cancellation covers selection,
an outstanding four-fragment window and a stalled final ACK. Disconnect tests
await modeled peripheral foreground cleanup, verify exporter release and test a
fresh download. Late GATT work is rejected without touching the next operation.
The extended browser simulations additionally exercise W=8/16 at MTU 23/247
with missing/corrupt/reordered frames, lost data/final ACKs and exact fixture
comparison. Focused cases prove a pending ACK write does not block reception,
ACK requests coalesce and progress callbacks are throttled/cleaned up.

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
