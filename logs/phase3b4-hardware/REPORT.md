# Phase 3B.4 controlled hardware identification

2026-10-31. **Upload and read-only diagnostic completed. Physical TCB3 recording
remains disabled and NO-GO.** COM8 was closed after verification.

## Firmware and upload

The uploaded application is the exact tested normal-build ELF:
`a83cbaabf9c0e18034a5f70a987539c77c574e429e9b6aa3a06caf6d1a836089`.
ZIP SHA256:
`d8ccaa11b734686fef5116d7be253fa3a2cef242bfc7b6831de44caa57559bb9`.
The ZIP application bytes were compared with objcopy output from that ELF before
upload. Its manifest contains application only, not bootloader/SoftDevice updates.
The linked binary contains DIAG ID and TCB DISABLED and no enabled TCB store symbols.

The established procedure succeeded: 1200-baud touch of the identified board on
COM8, bootloader enumeration on COM7, Adafruit nrfutil `dfu serial` with the
PlatformIO-generated ZIP at 115200 baud, `--singlebank`, then normal application
enumeration on COM8. nrfutil reported `Device programmed.` This programmed the
MCU application, not the external SPI recording array. Firmware/configuration
sources were not modified for this upload.

## Startup and stopped-state verification

* USB serial/menu, flash identification and read-only recovery scan succeeded.
* Startup: `GPS PROFILE: STARTUP 10HZ ALREADY VERIFIED (no writes; original snapshot unavailable)`.
* GPS STATUS before and after: `10HZ VERIFIED RAM`, MEAS=100 ms, NAV=1, GPS and
  Galileo enabled, BeiDou disabled, RMC=1, GGA=10, other selected NMEA messages
  disabled, UART1=38400. Valid fix, zero NMEA checksum failures. No manual GPS
  configuration or restore command was sent.
* V1 RECORD STATUS before and after both captures: STOPPED, session=0,
  committed_samples=0, buffered_samples=0, dropped_samples=0, missed_deadlines=0,
  recovered next_sector=`0x10000`. These counters describe this boot, not a
  historical count of old recordings.
* TCB STATUS before and after: `TCB DISABLED`.
* COMP: capturing=0, fault=0, accepted=0, sealed/completed=0, input/output=0/2.
* OBS initially defaults to passive RAM capture at boot (capture=ON,
  RAM_fast_imu=OFF). The initial precondition check correctly stopped before
  DIAG ID. OBS STOP and COMP STOP were then issued, and all stopped states were
  confirmed before either capture. No second upload was needed.
* Before stopping, passive OBS queues had overflow counters IMU=228, RMC=453,
  GGA=41. They were unchanged after the captures. These are idle diagnostic RAM
  drops, not V1 flash recording losses; this was not a 100 Hz timing test. No
  queue clear or recording start command was sent.

The first runner's failed precondition is retained in `run-result.json`; the
successful continuation and final state are in `capture-result.json`. Earlier
preflight scripting checks also stopped before hardware upload when checking
the package layout; they did not issue flash commands.

## Complete capture comparison

| Evidence | Capture 1 | Capture 2 |
|---|---|---|
| Five JEDEC reads | `0B4018` x5 | `0B4018` x5 |
| SR1 before / after | `00 / 00` | `00 / 00` |
| SR2 | `02` | `02` |
| Legacy ID, address zero | `0B17` | `0B17` |
| SFDP pass 1 versus pass 2 | All 256 bytes identical | All 256 bytes identical |
| Cross-capture comparison | All four 256-byte passes identical | PASS |
| Device errors / unsupported responses | None reported by these commands | None reported by these commands |

SFDP SHA256 (each pass):
`fb55c78de1f723eb5048b7f004658c75128c515881efae7d9dcc918afafafdec`.
The diagnostic's `qualification=UNRESOLVED` is a fixed evidence-only label, not
a device-generated identity or automatic compatibility decision.

Full 256-byte register contents, common to all four passes:

```text
0000 53464450000101FF00000109300000FF
0010 0B000103600000FFFFFFFFFFFFFFFFFF
0020 FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
0030 E520F1FFFFFFFF0744EB086B083B42BB
0040 EEFFFFFFFFFF00FFFFFF00FF0C200F52
0050 10D800FFFFFFFFFFFFFFFFFFFFFFFFFF
0060 003600279FF97764D9E8FFFFFFFFFFFF
0070 4541303334312D002020200300310031
0080 0202FFFF13061C5513070355FFFFFFFF
0090 FFFFFFFF5F464283833B655D3D754356
00A0 50528678FFFFFFFFFFFFFFFFFFFFFFFF
00B0 FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
00C0 FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
00D0 FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
00E0 FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
00F0 FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
```

Parsed SFDP evidence:

* Header: signature SFDP, revision 1.0, NPH=1 (two parameter headers).
* BFPT ID `FF00`, revision 1.0, nine DWORDs, pointer `0x30`.
* Vendor parameter ID `FF0B`, revision 1.0, three DWORDs, pointer `0x60`.
* BFPT little-endian DWORDs:
  `FFF120E5 07FFFFFF 6B08EB44 BB423B08 FFFFFFEE FF00FFFF FF00FFFF 520F200C FF00D810`.
* Density DWORD `07FFFFFF`: 134217728 bits = 16777216 bytes = **16 MiB / 128 Mbit**.
* Array address-byte field: three-byte addressing. Write-granularity flag is
  set (the legacy SFDP flag does not establish partial-program limits).
* Read descriptors: 3B (0 mode/8 wait clocks), BB (2/2), 6B (0/8), EB (2/4).
  The 2-2-2 and 4-4-4 support bits are clear. Advertised erase types: 4 KiB/20,
  32 KiB/52, 64 KiB/D8; fourth absent. These are decoded descriptors, not commands
  executed during identification.
* Vendor bytes at `0x60..0x6B`: `003600279FF97764D9E8FFFF`.
* Bytes at B's documented UID location `0x94..0xA3`:
  `5F464283833B655D3D75435650528678`, stable in every pass. This is a candidate
  UID interpretation, not authentication. Other non-FF bytes outside declared
  parameter tables are retained as opaque evidence; no lot/date/model encoding
  was invented from them.

## Datasheet assessment

The captured parameter-header bytes, table pointers/lengths, vendor table
constants and UID location strongly support a **B-compatible XTX fingerprint**.
The B document's SFDP density entry differs from the observed value and its own
128-Mbit description; its bit labels also contain inconsistencies, so this is
not an assertion that every printed table bit matches. Under its status map,
SR1=00/SR2=02 means WIP=0, WEL=0, BP=0, CMP=0, SRP=0, WPS=0, QE=1, security-lock
bits clear: no main-array block protection indicated. QE was already enabled;
it was not changed. [XTX XT25F128B Rev 1.2, status and SFDP tables](https://datasheet.lcsc.com/datasheet/pdf/8d867a672fb33d557ab1ba9942d04fab.pdf?productCode=C558846).

F shares these JEDEC/legacy IDs. Its SR2 interpretation also gives QE=1,
CMP/security locks/suspend flags clear; its separate WPS state was not read.
F documents a different UID read mechanism. Thus stable ID/status alone cannot
select an exact die/revision or explain the W25Q128 marking. No extra unsupported
opcode was probed. [XTX XT25F128F Rev 1.2, status and identification](https://cdn.semikey.com/upload/pdfs/7f/97/7f97109b0187d7aa9ee7c57f038dfe1c.pdf).

The existing single aligned 256-byte Page Program protocol remains consistent
with both candidates. That operation, its real timing and brownout recovery
were **not tested** here. Physical recording stays NO-GO pending resolution of
provenance/revision and the outstanding clock-gap/electrical power-failure
qualification, followed by a separately approved write-test plan.

## Preservation and final state

Only status queries, OBS STOP, COMP STOP and DIAG ID were sent. No RECORD START,
TCB START, RAM compression capture, destructive diagnostic, external program,
erase, WREN, status write or configuration-change command was requested. DIAG ID
uses only 05/35/90/9F/5A; array reads during startup recovery are read-only.

The external recording region and protected diagnostic sector were not targeted
for mutation. This identification test did not read back/hash the complete
recording array; preservation rests on the verified read-only command paths and
disabled recording state, not a claimed whole-array comparison.

Final states: **RECORD STOPPED; OBS OFF; COMP OFF/fault=0/queues empty; TCB
DISABLED; GPS 10 Hz RAM profile verified. COM8 released.**

Raw evidence: `upload.txt`, `startup.txt`, `serial-transcript.txt`,
`capture-transcript.txt`, `diag-id-1.txt`, `diag-id-2.txt`, `sfdp-capture-1.bin`,
`sfdp-capture-2.bin`, `decoded-evidence.json`, `preflight.json` and
`capture-result.json` in this directory.
