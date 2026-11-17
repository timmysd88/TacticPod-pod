# Working GPS standby and fast BLE - 17 November 2026

**WORKING — GPS STANDBY + FAST BLE — HARDWARE TESTED**

The existing prototype is running the exact application-only DFU image with
SHA256 `1ad514561794458b805bb41ee34af369891916c4067bff964b52f1863c498740`.

## Hardware results and their evidence

The controlled upload on 17 November completed once through the established
Adafruit serial DFU method, exit 0 / `Device programmed.` USB re-enumerated with
the same factory identity. Recorder remained STOPPED, flash identity/SFDP/status
checks passed, and no post-boot recording programs, CRC or incomplete-write
faults were reported. Complete pre/post TPX3 exports were byte-identical:
219,584 bytes containing clean sessions 2 and 45, reset epoch 9.
Export SHA256: `daf975e905f009735c8285f0395948a9ec4210ad4d5a3c810a37b809c94621dd`.
The 204,800-byte decoded images also matched exactly. Startup verified the
existing 10 Hz GPS profile at 38,400 baud; subsequent NMEA was valid with zero
checksum failures. The later RAM snapshot was marked stale by the normal
freshness policy; actual GPS cadence was not measured by that upload check.

In the preservation request, the user additionally confirmed physical testing
of GPS standby and wake, normal recording start/stop, GPS recording, high-speed
BLE downloads and successful import of downloaded recordings. These are user
bench confirmations for this exact working image; no additional hardware was
accessed during archival/commit/PR preparation, and no new automated bench log
or numerical throughput result is inferred from that confirmation.

Factory device identity, recording/storage formats, deliberate button admission,
flash ownership/deep sleep and the reviewed fast BLE implementation are retained.
Recovery from 9,600 to 38,400 baud is bounded, uses RAM-only writes and requires
baud readback plus the verified 10 Hz recording profile before admission.
Unsupported, missing or unverified GPS configuration fails closed.

## Software verification

All **48/48 firmware regression suites passed** on the complete fail-fast run.
The suite includes PMREQ/wake/baud/profile failures, battery freshness/admission,
recording/USB/storage preservation, queue credits, 8/16-frame transfers and
combined GPS-asleep BLE scheduling. All three builds passed:

| Environment | Static RAM | Application flash |
| --- | ---: | ---: |
| seeed_xiao_nrf52840_sense | 119,000 B | 167,964 B |
| tacticpod_reusable_usb | 123,468 B | 224,288 B |
| tacticpod_reusable_ble | 148,072 B | 340,604 B |

The source and artifact hashes were rechecked against the original validation;
no firmware behaviour or build inputs were changed, and nothing was rebuilt.
[Compact validation evidence](gps-standby-20261117-validation.json) preserves
suite results, build sizes and artifact hashes. Earlier Phase 1/integration
reports describe their historical pre-upload states; this report updates the
working image's hardware status without rewriting those records.

## Permanent archive and recovery

Archive: `C:/Tacticpod/TacticPod-recovery-artifacts/working-gps-standby-20261117/`
Exact working image: `firmware.zip` (SHA256 above). The archive preserves BIN,
HEX, ELF, map, configuration, source snapshot, commit provenance, complete
build/regression logs and controlled hardware test evidence, with checksums.
Raw recordings and location-bearing transcripts remain local, outside Git.

The separate GPS-safe recovery archive is retained unchanged:
`C:/Tacticpod/TacticPod-recovery-artifacts/gps-safe-ble-recovery-20261117T140708Z/`.
Recovery ZIP SHA256: `5d3363583a201fd34f0a2534a6830e6d28fb9e8693c865dc3aca2e25856e7e88`.
Its upload and normal initialization were demonstrated; the user reports
recording/BLE checks, but recovery from an already sleeping GPS remains unproven.

## Outstanding measurements

Idle current and battery runtime have **not** been measured. No numerical power
saving or battery-life claim is made. Longer repeated cycles, runtime RAM peaks,
factory-9600/full-power-loss recovery and measured RF/recording cadence remain
separate qualification items. Software standby is not physical battery isolation.
