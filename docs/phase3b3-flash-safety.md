# Phase 3B.3: one-program pages and sustained validation

2026-10-31. **NO-GO for physical recording, including a first controlled write
test.** All results below are host simulation or compilation results. No device
was opened, uploaded, programmed or erased. TCB3 remains disabled in normal
builds. The active V1 writer, RECORD READ CSV and frozen V2 specification are
unchanged.

## Device qualification

The installed capacity is **128 Mbit = 16 MiB**, not 128 MB. The saved startup
log `.pio/phase23-hardware/startup-serial.txt` reports five consistent JEDEC
responses `0B4018`, SFDP header `53 46 44 50 00 01 01 FF`, BFPT revision 1.0 and
134,217,728 density bits. It reports 4/32/64 KiB erase instructions and read
capabilities. It does not contain a complete raw BFPT or vendor parameter table.
The user's additional marking is `W25Q128`, without a suffix or supplier number.
These facts do not establish the exact manufacturer, model, die or revision.

Winbond's [W25Q128JV Rev F datasheet](https://www.winbond.com/resource-files/w25q128jv%20revf%2003272018%20plus.pdf),
sections 8.2.13 and 8.2.27, describes 1–256-byte Page Program to previously erased
locations, with write enable and CS held low through the command. It identifies
Winbond as `EFh`, which conflicts with the saved `0Bh`. The JV document therefore
cannot be declared applicable to this installed chip. Its Page Program section
is not evidence for an arbitrary five-programs-per-page allowance.

XTX's [XT25F128F Rev 1.2 manufacturer-authored datasheet](https://cdn.semikey.com/upload/pdfs/7f/97/7f97109b0187d7aa9ee7c57f038dfe1c.pdf)
was also reviewed as a candidate, not an identification. Section 5.2.10 describes
1–256-byte Page Program. Its timing tables specify 2 ms maximum at industrial
temperature and 3 ms for the extended-temperature grade. Those limits are not
assigned to the installed chip. The [manufacturer catalog](https://www.xtxtech.com/en/Products/info.aspx?productModel=XT25F128FWOIGT)
offers separate revision/migration material; this is another reason not to infer
a die revision from a density code. The reviewed programming section does not
establish the old five-partial-program guarantee.

Documents were reviewed through the web tool; direct local PDF downloads were
blocked by the environment's socket policy. `benchmarks/flash-safety/sources.json`
records URLs and this archival limitation. Identification and the applicable
programming, power-failure and clock-pause restrictions remain unresolved.

## Persistent container 3.2.0

This is a separately versioned change to the outer persistent container, not to
the RAM TCB3 block format, frozen V2 or V1. The historical
[container 3.1 specification](recorder-tcb3-persistence.md) continues to describe
old files. All byte layouts, page magics, metadata fields, sequences, fragment
rules, footer accounting and TPX3 packet envelopes from that document remain
unchanged except:

* Header metadata version at page offset 16 is `0x00030200` (little endian).
* TPX3 BEGIN declares `0x00030200`; export packet protocol remains version 1.
* The marker at page offset 252 is an **inline validity seal**, programmed in
  the same operation as bytes 0–251, rather than a later temporal commit.

The writer emits only 3.2.0. The decoder accepts 3.1.0, 3.2.0 and mixed images;
old 3.1 golden outputs and bytes remain unchanged. Raw compressed frames remain
byte-identical to their RAM output.

### Programming sequence

1. Verify the complete new sector is blank before its header. Verify each
   destination page is blank again in four 64-byte reads. Occupied or uncertain
   old sectors are never reused, including incomplete old sessions.
2. Prepare the complete 256-byte page in RAM: envelope, payload/padding, IEEE
   CRC32 of bytes 0–247 at 248, and `0xC0DEC0DE` seal at 252.
3. Check idle status and WEL, issue WREN and confirm WEL, then issue one `02h`
   Page Program with aligned 24-bit address. Keep CS low across four 64-byte
   foreground transfer slices. Acquisition can run between slices. No other
   flash/SPI transaction is allowed while this transaction owns the bus.
4. Raise CS once after exactly 256 data bytes. This is **one program operation
   per page**, not four partial programs. Poll BUSY every 2 ms without spinning;
   fail on the 1-second timeout, uncertain status or retained WEL.
5. Read back all 256 bytes in four slices, compare every byte, and validate CRC
   and seal. Only then count the page/block as persisted and release its RAM
   lease. Errors lock the writer and preserve retained accounting.

At most 16 pages/program operations are used per sector: header, 14 data pages
and optional final footer. The engine has no erase API and never revisits a
programmed page. Aborting a held-CS transfer can initiate a partial program;
that address is not retried, the writer faults, and reboot scanning conservatively
reserves occupied sectors. A reset with CS still asserted is also tested.

An inline seal is **not proof that programming completed atomically**. Recovery
requires a valid header, session/sector/page sequencing, page CRC and seal,
continuous fragments, complete frame CRC and valid typed contents. Interrupted
frames are discarded while previous complete frames remain recoverable. A fully
valid page that reached flash before software acknowledgement may be recovered.
CRC32 has a residual undetected-error probability and is not an adversarial
integrity mechanism. Simulation cannot prove arbitrary electrical failure
behavior or collateral damage outside an addressed page.

The installed Adafruit SPI implementation uses software CS and does not mask
interrupts across beginTransaction/endTransaction. TIMER4, UART and I2C can
continue during the held-CS gaps. This source review does not establish that
the unidentified flash permits the required clock pauses, or certify physical
IRQ latency. No SPI sharing is allowed during a held page transaction.

## Address and mutation safety map

| Range | Use and policy |
|---|---|
| `0x000000–0xFFEFFF` | 4095 recording sectors, append only into completely verified blank space |
| Sector page 0 | Session/continuation header |
| Sector pages 1–14 | 216 frame bytes per data page, with fragment envelope |
| Sector page 15 | Optional stop/footer; never used for frame payload |
| `0xFFF000–0xFFFFFF` | Protected diagnostic sector: no store/export access, program or erase |

Mixed V1/V2/TCB3 and torn/nonblank headers remain occupied. Allocation begins
after the highest occupied sector, not in gaps. Full storage stops capture and
accounts for unpersisted records; it never wraps. Read-only startup does not
resume an old session. Port checks reject crossing pages and the protected
boundary. No chip erase, protection-register change or erase function was added.

## Throughput results and limitations

The old 1.82–1.92-second maximum was dominated by foreground state transitions,
five independent self-timed program cycles and repeated readbacks per page,
not the compressed-byte SPI bandwidth alone. Full-frame CRC validation also
costs 129 foreground slices for an 8228-byte frame; sector blank verification
costs 64 read slices per sector. The new writer reduces each page from roughly
38 service ticks to 19 under the 1 ms service/3 ms BUSY model and uses a 64-byte
ROM nibble table for CRC, preserving the same polynomial and results.

The four-block maximum-frame regression now reports **1040, 1120, 1120,
1120 ms** persistence rather than 1820–1920 ms. These include sector boundaries
as encountered, not initial startup scanning. Maximum frame size is 8228 bytes,
requiring 39 fragment pages; test payloads use valid raw-fallback EVENT records
to exercise the hard frame-size limit independently of typical sensor entropy.

Whole-chip tests offer one maximum-size frame every 2000 ms until storage fills:

| Per-page BUSY | Reserved no-store-service time per block | Max arrival to commit | Margin to 2 seconds | Output queue high |
|---|---|---|---|---|
| 3 ms | 0 ms | 1118 ms | 882 ms | 1 |
| 3 ms | 400 ms | 1518 ms | 482 ms | 1 |
| 10 ms | 400 ms | 1770 ms | 230 ms (11.5%) | 1 |

Each scenario persisted 1470 maximum frames, ran about 2940 simulated seconds,
and issued 61,426 page programs, **never more than one per page**. Every complete
frame was independently decoded and compared; the protected canary remained
unchanged. The 1471st offered frame encounters finite storage exhaustion and
is explicitly accounted as unpersisted. There is no increasing backlog within
this repeated timing envelope; arbitrary BUSY times, scheduler stalls or CPU
costs are not covered by that claim. A separate 250 ms/program overload case
fills the bounded queue, stops safely and reports loss instead of overwriting.

A 120-second integrated high-entropy IMU/GPS run used the production ARM code,
1 MHz simulated SPI, 10 ms BUSY per program, 180 us per IMU read and an injected
100 us cost per active compression work unit. It recovered **12,000 IMU,
1,200 RMC and 120 GGA records**, exactly matching 345,600 original wire bytes.
There were zero missed deadlines, rejects or losses; IMU spacing was
9908–10092 us, input slot high-water 2 and output slot high-water 1. Sixty blocks
used 837 data pages and 898 total programs. Maximum store block time was 357 ms
and maximum modeled SPI service was 544 us. UART observations are simulated
sentence arrivals, not physical receiver/interrupt measurements.

SPI service numbers model wire time; per-byte Arduino calls, IRQ interference
and real electrical/programming latency need measurement. The injected CPU
cost is a stress assumption, **not a measured embedded WCET**. This validation
demonstrates bounded behavior under stated conditions, not hardware readiness.

## RAM, regressions and reproducibility

Normal firmware builds with TCB3 disabled: **118,440 bytes static RAM** and
161,060 bytes program storage. Separately enabled sizing build: **120,140 bytes
static RAM** and 178,136 bytes program storage. This is +64 bytes over the prior
enabled build and +1700 bytes over normal. The existing two 8192-byte input
slots and two 8228-byte output slots are retained; no dynamic allocation was
introduced. The native integration harness observed 856 stack bytes for its
instrumented paths; physical full-system stack/heap high-water is unqualified.
The 16 MiB simulated chip and large expected-data arrays exist only on the host.

All 14 regression suites pass, including both whole-capacity and concurrent
long-run tests. The store suite has 34 tests. Interruption testing covers 2570
prefix cut positions across ten full-page programs, 2890 reverse/random
programming-order cases (including early seal cells), and reset at all five
wire-slice boundaries with both held and released CS. Existing corruption,
torn commit, mixed-format, continuation, flash-full, timeout, queue-pressure,
parser-ownership and export tests remain enabled.

Reproduce with the project's PlatformIO Python and `run` build. The added
standalone suites are `tests/tcb3/sustained_tests.py` and
`tests/tcb3/long_integration_tests.py`; existing store/integration/console suites
remain separate. Per-suite pass/fail results are retained in
`.pio/phase3b3-regressions.json`. Durable result snapshots and source evidence are
under `benchmarks/flash-safety/`.

The new `tests/tcb3/fixtures/v32/example.tpx3` fixture exports and decodes 600 IMU,
60 RMC and 6 GGA records to its 17,280-byte `expected.v2wire`, exactly. Example:

```powershell
python tools/tcb3_decode.py tests/tcb3/fixtures/v32/example.tpx3 --raw-output new-output.v2wire
```

The output path must not already exist. This command only reads local files.
The historical 3.1 fixtures remain unchanged and are still regression inputs.

Changed production files: `src/tcb3_store.h`, `src/tcb3_store.cpp`,
`src/tcb3_session.cpp`, `tools/tcb3_decode.py`. `src/compact_recorder.cpp` adds
only a compile-time simulation timing hook, inactive in firmware. Test changes
cover the NOR port, full-chip pacing, integration stress, SPI ownership and
3.1/3.2 decoder fixtures. No active V1 writer or flash partition changes occur.

## Gate for a later hardware test

**NO-GO remains in force.** First reconcile the W25Q128 marking with `0B4018`
using full top markings/supplier traceability and complete read-only identity
evidence. Obtain the applicable exact-revision specification for a single
full-page program, clock pauses while CS is low, power interruption behavior,
and worst-case programming time. Then review a controlled test plan protecting
all existing sessions, measure actual SPI/CPU/IRQ and RAM high-water behavior,
and obtain explicit authorization before any upload or physical write. The
single-program design removes dependence on a repeated-partial-program
allowance, but it does not resolve the chip identity or certify electrical
power-failure recovery.
