# Phase 3B.10 - supervised 120-second movement test

Recorder integrity and cadence passed, with the IMU count, GPS availability and
sensor-range qualifications below. One START, automatic stop, clean finalisation,
two identical binary exports and three independently decoded sessions. No retry,
erase, manual STOP, status-register write, unrestricted recording or power-cut
experiment occurred. COM8 is released.

## Firmware and preflight

Uploaded only `logs/phase3b10-review/tcb3_movement_120s-3b10.zip`, SHA-256:
`0bdd99698bd4535dfe02943a045d3c9b697a4f5f3d92b077857d7f4f8d9c059e`.
Application payload SHA-256:
`d47576995adb23fb831502e4066148816c5aadd40328fe17ff2f189343868900`.
All 20 regression results passed; all reviewed source/artifact hashes matched
before upload, and source/archive hashes still match after testing. No firmware
source or configuration was modified. These hashes verify the submitted payload;
no independent target firmware readback hash was obtained.

The established application-only PlatformIO-packaged nrfutil procedure used a
1200-baud touch on the identified XIAO, bootloader COM7, then application COM8:

```text
adafruit-nrfutil.py dfu serial -pkg logs/phase3b10-review/tcb3_movement_120s-3b10.zip -p COM7 -b 115200 --singlebank
```

DFU reported Device programmed. Stable boot and USB confirmed the runtime marker
`test=120s,1MiB,180s profile=3B10 exact=B000`: exact start 0x00B000, session 12,
120-second capture, 180-second total deadline, writes only 0x00B000-0x0FFFFF,
all erases/V1 writes disabled and no automatic recording.

JEDEC was repeatedly 0B4018, SFDP capacity 134217728 bits / 16777216 bytes / 16 MiB.
SR1 before/after was 00 and SR2 02. Both SFDP passes matched the saved reference
before and after recording. No new protection/status fault was seen. DIAG ID's
static qualification=UNRESOLVED label for exact chip model/revision remains;
this trial does not settle physical-chip provenance beyond its established
XT25F128B-compatible fingerprint.

GPS verified the existing RAM-only 100ms MEAS / NAV 1 profile, GPS+Galileo,
BeiDou off, RMC each epoch, GGA every tenth epoch, other configured sentences off,
UART 38400. No GPS test/restore, nonvolatile receiver write or power cycle occurred.
RECORD STOPPED, OBS OFF, COMP OFF/empty, TCB idle/zero programs and read-only
recovered append 0x00B000 were confirmed.

Two preflight exports and another immediately before START matched the archived
49344-byte combined export exactly, SHA-256
`8e22bf5ad18f8a98c8d9ef7535603f6d882c32d2a0fc97362375204b1cef3240`.
Both earlier sessions decoded completely: session 1 has 500/50/5 observations,
session 3 has 3000/300/30, with unchanged footer counts and canonical bytes.

After upload, the device remained idle and COM8 was released while waiting for
the operator. Only after the operator replied ready did the host recheck state,
identity and the old export, then send the single START. Movement-start and
capture-stopped cues were delivered. Recorded axis variation supports that the
device moved; no particular exercise label is inferred.

The whole allocation was not independently pre-scanned. The approved method
relied on the archived erase, known occupied prefix and full-sector checks before
each allocation. STARTED session 12 with skip=0 establishes that the firmware's
complete 4096-byte initial-sector probe passed. It rechecked that sector before
its header, each further sector before allocation, and every page before
programming. The first program address/session was checked again before WREN.
This is firmware/readback evidence, not an independent raw-sector read or bus trace.

## Capture counts and timing

Automatic STOPPED reported 60 blocks, 13321 offered/committed observations,
681 full-page programs and error=none. Host START-command to STOPPED notice was
121.937 seconds, including read-only allocation scan and finalisation.
Metadata-start to capture-stop was 120.000145 seconds. Metadata slightly precedes
the guard's capture origin; this does not measure its precise overshoot.
The 180-second deadline did not fire.

| Stream | Count | Timestamp-derived mean rate | Minimum / maximum interval |
| --- | ---: | ---: | --- |
| Six-axis raw IMU | **12001** | 100.003668 Hz | 8991 / 10988 us |
| RMC | 1200 | 10.000563 Hz | 78627 / 119298 us |
| GGA quality | 120 | 1.000079 Hz | 979097 / 1018362 us |
| EVENT | 0 | - | - |

The IMU count is **not exactly 12000**. It falls within the explicitly simulated
12000/12001 boundary tolerance: 12000 positive intervals span 119995599 us inside
the capture window. Mean interval 9999.633 us, standard deviation 416.970 us,
maximum absolute interval error 1009 us relative to 10000 us. All samples remain
preserved; none was removed to force the requested count. No duplicated timestamp,
reported missed deadline, rejected sample or fabricated replacement was observed.
Reusable firmware should document its time-window versus exact-count policy.

All new timestamps advance strictly, including cross-stream ordering: no equal or
backwards timestamps. Sessions retain separate boot time origins. This physical
run did not cross the TIMER4 low-word rollover; that remains covered by simulation.
Footer reason clean; offered=committed=[12001,1200,120,0], lost=[0,0,0,0]. Rejected,
missed, full, CRC-failure and incomplete-write counters are zero. All page CRCs,
commit seals, block CRCs, export packet CRCs, metadata, sequences and footer
accounting passed. Block sequences 0-59, sector sequences 0-45, data-page sequences
0-633 and footer sequence 634.

## GPS and diagnostics qualifications

All 1200 RMC UTC epochs are unique and advance exactly 100ms. **1033 fixes are valid
and 167 invalid**, matching status A/V counts. Every invalid RMC position decodes
as null, retaining its explicit validity mask rather than stale coordinates.
All 120 GGA epochs are unique and advance exactly 1000ms: 104 quality 2, 16 quality
0. RMC masks 112/119/127 and GGA masks 49/51/63 preserve optional-field availability.

This demonstrates preservation of loss-of-fix observations and complete output
epochs, not continuously valid navigation for the whole test. Final GPS STATUS
was NO FIX and labelled its diagnostic position LAST_KNOWN/UNAVAILABLE. The 10Hz
RAM profile remained verified. Reception and placement need assessment in the
intended mounting; no cause for fix degradation is established. HDOP is not a
measured accuracy in metres.

Recording/compressor queues had no rejection, overflow, fault or backpressure.
OBS's startup diagnostic overflows (RMC 191, GGA 15) remained unchanged with OBS
OFF; those diagnostic queues are bypassed after forwarding to the compressor.
Parser checksum/malformed/overlong counters stayed zero. The parser partial-frame
counter increased **0 to 1**. That is consistent with restarting a previously
paused sentence collector after the long idle wait, but is an inference because
this counter has no event timestamp. No complete RMC/GGA UTC gap or checksum
failure was observed. The counter was not cleared or hidden; reusable firmware
should test capture-boundary parser state explicitly.

## Movement and sensor range

| Raw axis | Minimum | Maximum |
| --- | ---: | ---: |
| Accel X | -5403 | 15524 |
| Accel Y | -4981 | 27716 |
| Accel Z | -15888 | 22512 |
| Gyro X | -23222 | 16002 |
| Gyro Y | **-32762** | 18621 |
| Gyro Z | -16023 | 15018 |

One gyro-Y value had absolute magnitude >=32700; none equalled an exact int16
endpoint. The near-rail value alone does not prove clipping, but range headroom
for faster sport movement is not qualified. Original signed values and full
recorded precision remain intact. No sensor range/sensitivity was changed.

## Binary export and preservation

Both raw exports are **255424 bytes**, identical byte-for-byte, SHA-256:
`03c489ffd50a17fb190b7c9d1603ec96c983b67e61136d80a2d7d18ac3eaf3a9`.
Files: movement-120s-1.tpx3 and movement-120s-2.tpx3; no text conversion was used.

All three sessions were isolated into independent TPX3 exports with packet
sequence/CRC reframing, then decoded through both the library and standalone
`tools/tcb3_decode.py`. Canonical bytes agree exactly: new session 345624 bytes,
combined 446424 bytes. Sixty new compression frames total 130348 bytes.
The first 45056 flash bytes are unchanged. Extracted session-1 and session-3
exports match their individual archives; earlier-two-sessions.tpx3 also matches
the entire archived combined export exactly. No independent RAM/sensor tap was
recorded; fidelity is supported by the tested lossless codec, CRC/readback chain
and decoder agreement rather than a simultaneous second capture.

## Actual recovered flash use

New allocation: **46 sectors / 188416 bytes**, **0x00B000-0x038FFF**. Next recovered
append position **0x039000** (future address-derived session ID 58). The currently
installed used one-shot profile cannot start there.

Runtime 681 programs match 681 recovered committed pages / 174336 programmed
bytes: 46 headers, 634 data pages and one footer. In the first 45 sectors, starting
0x00B000 through 0x037000, page 0 is the header, pages 1-14 data and page 15 FF.
The last sector has header 0x038000-0x0380FF, data 0x038100-0x0384FF, unused FF
0x038500-0x038EFF and footer **0x038F00-0x038FFF**. The exact per-page map is in
new-session-pages.csv; summary.json contains the sector map.

All recovered new pages are within 0x00B000-0x0FFFFF. No diagnostic-sector operation
was targeted. No independent SPI trace or outside-fence/diagnostic-sector full
byte comparison was obtained: range guards, program-count reconciliation and
recovered pages are the available evidence for the address map.

## Performance and resource measurements

| Maximum measured metric | Value |
| --- | ---: |
| Flash store service call | 1189 us |
| Block persistence (store metric) | 487 ms |
| Compression CPU work per block | 54637 us |
| Compression slice | 264 us |
| Input / output queue high-water | 2/2 / 1/2 slots |
| Backpressure slices / raw fallbacks | 0 / 0 |

Block CPU work is accumulated across slices, not a blocking call; block
persistence is not individual chip page-program BUSY duration. No growing backlog
was observed. These maxima apply to this run, not every future workload.
Static RAM is 120692 bytes. Live stack/heap high-water, current draw, battery
runtime, endurance, long-duration capacity and environmental performance remain
unmeasured or unqualified by this test.

## GO / NO-GO

**GO for separately reviewed reusable-firmware development**, retaining bounded
sessions, append-only allocation, complete blank checks, no automatic erase or
recording, fault locks and legacy compatibility. Document the count-boundary
policy, test parser state transitions, review gyro headroom and assess reception
in the intended mounting. This does not authorise unrestricted recording, another
capture or deletion of any of the three sessions.

**GO for battery-integration design and bench-qualification planning; NO-GO for
battery-powered recording/deployment yet.** This USB run did not validate the
actual battery/charger/protection/power path, voltage margin, current/runtime,
USB/battery transitions or undervoltage/brownout handling. Those require separate
hardware review and authorisation. No battery wiring or power-cut experiment was
performed or approved here.

Final state: RECORD STOPPED, OBS OFF, COMP OFF/fault=0/queues empty, TCB idle and
finalised. Bounded single-capture firmware remains installed and used. GPS retains
the 10Hz RAM profile, with final NO FIX. **COM8 released.** No erase, retry,
additional START or further hardware action followed the test.

Evidence is saved in summary.json, analysis.json, decoded.json, raw/isolated TPX3
exports, canonical wire files, standalone CLI outputs, page CSV, readiness
acknowledgement, artifact checks and complete upload/startup/preflight/record logs.
