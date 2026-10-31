# GPS 10HZ TEST failure investigation

The corrected build is **not uploaded**. No serial port was opened and no GPS
configuration or TacticPod flash operation was executed during this investigation.

## Finding

The previous firmware sent three VALSET packets, with actions START (1),
CONTINUE (2), APPLY (3). The third packet incorrectly carried all measurement,
navigation and NMEA message-rate pairs. SPG 5.10 section 4.6 specifies that
key/value data in the APPLY state is ignored. Thus the first two packets could
change constellation settings, while MEAS and message rates remained unchanged,
even though the receiver ACKed all three packets.

This is a confirmed protocol defect and the strongest explanation for the
hardware's first mismatch at `0x30210001`. The earlier implementation also
discarded the actual failed value when it began rollback and printed restored
values using a generic requested/readback label. The reported restored 1000 ms
therefore is not, by itself, a captured measurement of the pre-rollback value.

The original simulated receiver incorrectly parsed and applied key/value pairs
from APPLY, masking the same defect. Correcting the model to ignore APPLY
payload reproduces the failure with the old packet sequence: BeiDou is disabled,
MEAS remains 1000 ms, and GGA remains divisor 1. The corrected firmware passes
against that stricter model. A permanent negative fixture retains the original
failure reproduction.

## Wire-format and transaction audit

`CFG-RATE-MEAS` is key `0x30210001`, type U2, units milliseconds. The encoder
was already correct: the key/value bytes for 100 ms are
`01 00 21 30 64 00`. The saved 1000 ms value encodes as
`01 00 21 30 E8 03`. NAV=1 encodes as `02 00 21 30 01 00`.
There is no padding between key/value pairs and no change to these values or
their type to make verification pass.

Both TEST and RESTORE now use this sequence:

| Stage | Action | Payload after version/layer/action/reserved | Typical full UBX length |
| --- | --- | --- | --- |
| 1 | START (1) | First nine signal key/value pairs | 57 bytes |
| 2 | CONTINUE (2) | Remaining five signal key/value pairs | 37 bytes |
| 3 | CONTINUE (2) | MEAS, NAV and six UART1 NMEA rates | 54 bytes |
| 4 | APPLY (3) | **Empty** | 12 bytes |

All use version 1, RAM layer bitmask 1, and reserved byte 0. Data packets may
duplicate their last pair on retry to avoid a stale DMA length; APPLY never does.
The complete empty APPLY frame is:

```text
B5 62 06 8A 04 00 01 01 03 00 99 1B
```

Only a checksum-valid matching ACK advances a write stage. NAK, timeout or
uncertain transmission still triggers the existing automatic rollback path.
Out-of-phase VALSET ACK/NAKs are now logged and counted, not used to advance.
The protocol only identifies class/ID in an ACK, so severely delayed replies
cannot be perfectly correlated to stages; final complete readback remains the
success criterion.

## Timing and constellation review

The 600 ms post-commit-ACK settling margin is unchanged. The official signal
configuration rule asks for 500 ms after acknowledgement of a GNSS restart.
The old code read fourteen signal keys before MEAS, with 200 ms gaps; therefore
MEAS was polled about 3.4 seconds or more after the commit ACK. Increasing the
delay would not make ignored APPLY payload take effect. Tests check these
timing margins and the diagnostic reports actual elapsed commit age.

The profile still enables GPS L1 C/A and Galileo E1, retains/enables QZSS
L1 C/A support, preserves SBAS and other supporting flags, and disables BeiDou
and GLONASS through their constellation enables. BeiDou B1I is also disabled.
This avoids the B1I versus B1C/GLONASS signal restriction. FULL power operation
and the existing UART/protocol guards remain prerequisites.

The MAX-M10S data sheet lists GPS+Galileo at 10 Hz with default CPU clock.
This does not require CPU/OTP programming. Actual navigation availability and
cadence still need measurement after a separately approved hardware test.
There is no evidence in the supplied log that lack of a fix or UART bandwidth
caused this configuration readback failure.

## New diagnostics and unchanged protections

For an induced 1000 ms mismatch in simulation, the trace records the equivalent
of this **before scheduling rollback** (timestamps vary):

```text
GPS TRACE TEST ... READBACK MISMATCH key=0x30210001 requested=100 actual=1000 commit_age_ms=... (captured before rollback)
GPS TRACE RESTORE ... ROLLBACK BEGIN; original snapshot retained
```

The final result preserves this distinction even after successful recovery:

```text
RATE_MEAS_ms [0x30210001] saved=1000 test_requested=100 test_actual=1000 restore_requested=1000 restore_actual=1000
```

Each TEST/RESTORE stage reports action, transmitted bytes, number of pairs,
ACK/NAK/timeout and reply latency. Per-key rows distinguish NOT_READ, TIMEOUT,
NAK and actual values. The first mismatch and both readback sets live in fixed
RAM structures. USB backpressure cannot postpone automatic recovery; queued
messages retain their capture time, and any trace overflow is explicitly counted.
The first-mismatch summary survives overflow independently of the trace queue.

No verification is relaxed: the first test mismatch still initiates rollback,
and all 22 written values plus seven guards must match before a success report.
The same corrected four-stage protocol restores the snapshot. Recording/busy
interlocks, RAM-only settings, snapshot retention after failed recovery,
timeouts, bounded buffers and TacticPod flash protections remain in place.

## Validation and references

Regression cases include the old APPLY-payload failure, golden U2 encodings and
empty APPLY bytes, before-rollback mismatch evidence, USB backpressure/overflow,
commit NAK/timeout retention, duplicate ACKs and settling delays. All original
flash, recorder and GPS tests are also run. This is simulated validation,
not confirmation of a second hardware test.

Final validation: PlatformIO build passed without compiler/linker errors or
warnings; RAM 70,004 bytes (29.5%), firmware flash 127,464 bytes (15.7%). All
178 simulations pass: 13 flash, 20 recording, 145 GPS/UART. Nine new regressions
cover this defect and its diagnostics. The hardware remains on the previously
uploaded firmware until another upload is explicitly approved.

- [SPG 5.10 interface description](https://content.u-blox.com/sites/default/files/u-blox-M10-SPG-5.10_InterfaceDescription_UBX-21035062.pdf): section 4.6 / page 127, APPLY payload ignored; section 3.10.5, transaction actions; CFG-RATE and CFG-SIGNAL definitions.
- [MAX-M10S integration manual](https://content.u-blox.com/sites/default/files/MAX-M10S_IntegrationManual_UBX-20053088.pdf): signal dependencies and QZSS support.
- [MAX-M10S data sheet R08](https://content.u-blox.com/sites/default/files/MAX-M10S_DataSheet_UBX-20035208.pdf): default-clock GPS+Galileo navigation rate.
