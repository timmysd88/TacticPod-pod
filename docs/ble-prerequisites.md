# BLE prerequisites assessment (investigation only)

Status: investigation. No firmware, flash, session or app changes were made. Labels: **[M]** measured in repo evidence, **[E]** estimate, **[?]** needs bench verification.
This supersedes the round-1 BLE trigger proposals (auto-advertise at power-on / after stop / long-press) in the app repo's `docs/BLE_AUTO_DOWNLOAD_INVESTIGATION.md`, which is left unchanged. Button-driven Bluetooth Mode below replaces them.

## 1. SoftDevice / RAM / flash

- Latest documented build: RAM 122,040 / 237,568 B (51.4%), flash 202,320 / 811,008 B (24.9%) [M] (`docs/field-trial-fixes.md`).
- The Adafruit nRF52 core linker script already reserves the S140 layout (flash from 0x26000, RAM from 0x20006000), so the ~24 KB SoftDevice RAM block and ~152 KB SoftDevice flash are already excluded from those totals [M, upstream core]. `Bluefruit.begin()` calls `sd_ble_enable`; if the requested config needs more RAM than the linker provides it fails at runtime (logged), and the only fix is moving the RAM origin (cost: app RAM).
- Estimated additional: flash +60–100 KB (Bluefruit52Lib, GATT, tasks) [E]; app RAM +8–16 KB for Bluefruit objects, task stacks (BLE, SOC, streamer), 247-byte-MTU buffers and a ring buffer, and possibly more SD RAM if the default high-bandwidth config is raised [E][?]. Headroom (115 KB RAM, 600 KB flash) is ample [E].
- Environment supports BLE without changing the recording architecture: the project is already Arduino + Adafruit-core-based (Seeed fork). Add a **separate env** `tacticpod_reusable_ble` (`-DTACTICPOD_BLE=1`) so default envs and their binaries stay unchanged.
- **Could not build here**: `files.seeedstudio.com` (framework/toolchain host) is unreachable, so RAM/flash numbers for BLE are not reproduced. Real `pio run` size and the `Bluefruit.begin()` RAM log are a bench/CI prerequisite.
- Conflicts/risks:
  - Loop task is lowest priority (1) and busy-spins; Bluefruit/SOC tasks are priority 3, timers 2. BLE tasks will pre-empt the loop (see timing risk below) [E, upstream core][?].
  - `BLECharacteristic::notify()` blocks on a semaphore (`BLE_GENERIC_TIMEOUT`) when the HVN queue is full, so it must run only in a dedicated streamer task, never in `loop()`.
  - `Bluefruit.begin()` calls `bond_init()` → InternalFS. Internal flash writes stall the CPU (~85 ms [E]); v1 uses no bonding and must verify nothing is written at boot.
  - No public SoftDevice-disable API in Bluefruit. Full teardown + re-`begin()` is fragile [?]; compare against "SD enabled, advertising stopped" (see §3).
  - SD-reserved peripherals/IRQ priorities: firmware uses TIMER4 (IRQ prio 7), UARTE0, TWIM1, SPIM, FICR; no direct POWER/CLOCK/NVMC/RTC use. No peripheral conflict found by inspection, but priority-0/1/4 IRQs are SD-reserved and must not be used [E].
  - Adafruit_TinyUSB coexistence with SD (USB CDC `TCB READ`) is supported upstream but [?].

## 2. Recording performance baseline

Only real-hardware evidence: `logs/phase3b10-hardware/REPORT.md`, one 120 s supervised trial, **BLE absent** [M]:

| Metric | Result |
|---|---|
| IMU samples | 12001, mean 100.0037 Hz |
| IMU interval min/max | 8991 / 10988 µs; σ 416.97 µs; max abs error 1009 µs |
| GPS RMC | 1200 samples, 10.0006 Hz (78.6–119.3 ms) |
| GGA | 1 Hz |
| missed/rejected/full/CRC/incomplete-write | 0 |
| Queue overflow | none |
| Compression block CPU | ≈54 ms max (`max_block_step_us`) |
| Block persistence | 220–487 ms (`max_block_ms`) |

Caveats: single short trial, not worst-case; parser partial-frame counter 0→1 after idle; stack/heap high-water, CPU load and power not measured. IMU is polled (no FIFO/interrupt) in a busy loop, and flash I/O is foreground ≤64 B slices, so any BLE task that pre-empts the loop for several ms directly adds IMU jitter. A 10 ms sample can be late by the pre-emption time, and the IMU read is timestamped by TIMER4 (so jitter is recorded rather than hidden, but late reads cost fidelity). GPS UART RX relies on Serial1 buffering; long pre-emption risks overrun at 38400 baud (~3.8 B/ms).

Host tests: this sandbox ran only 6/34 suites (see §6). Host/Unicorn tests demonstrate logic, not hardware timing.

Needed baseline additions (bench): loop-period histogram, max loop gap, IMU late/miss counters, GPS UART overrun count, flash-write latency distribution, stack/heap high-water, over ≥3 sessions of ≥15 min each in the **default** build, to compare to BLE-capable builds with BLE idle. Because BLE is never active during recording (§4), the key comparison is "BLE-capable build, BLE never entered" vs default; plus the "SD enabled but idle" cost, if that strategy is chosen.

## 3. Power baseline

Existing management (`src/standby_policy.h`, `main.cpp`): standby 2 s after idle unless recording, session busy, fault, USB, serial open, capture, start attempt, GPS busy, button busy or LED busy. In standby: flash deep power-down, IMU power-down, GPS UART suspended, TIMER4 stopped, `standbyIdle()` polls D0 every 50 ms with `delay()` (tickless idle). Unmerged branch `origin/standby-power-optimisation` changes GPS backup standby and idle poll rate; it must be reconciled before any power numbers are trusted. **No current measurement exists in the repos; all power figures are estimates.**

Expected BLE effect [E]: advertising at 100–250 ms interval ≈ 0.05–0.4 mA average over 180 s (≈ 0.01–0.07 mAh per window); idle connection ≈ 0.02–0.1 mA at 100–500 ms interval; active download ≈ 4–8 mA (plus flash/CPU); wake-from-standby requires leaving the standby path, so the CPU, flash release and IMU/GPS re-init costs apply. On a 300 mAh cell a single 3-minute window is <0.1 % of capacity. The dominant risk is a wake-lock that never releases.

Physical measurements required (PPK2/Otii, supervised): (1) current standby, default build; (2) standby polling rates 50/25/20 ms and a GPIOTE variant; (3) BLE-capable build in standby with BLE never entered; (4) advertising 180 s at chosen interval; (5) idle connection; (6) active download; (7) SD enabled-idle vs fully torn down; (8) recording with BLE-capable build vs default; (9) double-tap wake cost. GPS and recording power behaviour are not to be modified.

## 4. Button-driven Bluetooth Mode

### Current button facts [M]
D0/P0.02, active-LOW, `INPUT_PULLUP`, **polled** (no interrupt). `kDebounceMs=25`, `kHoldMs=3000`, `kTapMaxMs=500`, `kSecondPressTimeoutMs=5000` (`src/physical_button.h`). Idle: only a ≥3 s hold (armed, not blocked) → Start; a short tap does nothing. Recording: tap ≤500 ms then 2nd press held ≥3 s within 5 s → Stop. `armed` is false until the button is released after boot.

### Feasibility of double-tap
Idle double-tap is a free gesture (no existing action). It does not conflict with the Start hold if the second press is classified only once released before 3 s. Conflict cases: tap then hold-to-start (tap + ≥3 s hold) — must remain Start's behaviour only if the pod's existing semantics accept it; proposal: a hold ≥3 s always wins over a prior tap and cancels the pending double-tap. Recording stop gesture (tap + hold) is unaffected because BLE tap detection is disabled while recording.

In standby the 50 ms poll can miss a quick double-tap (taps ~80–150 ms, gap ~100–250 ms). Options:
1. Reduce standby poll to 20–25 ms: simple, no new interrupt; cost ≈ 2× wakeups [E, needs measurement].
2. GPIOTE/PORT low-power interrupt wake on D0, then poll at 10 ms only after first edge: lower standby cost; Adafruit `attachInterrupt` mapping (IN vs PORT event) must be verified in the Seeed fork [?], and it changes standby code (needs approval).
3. Keep 50 ms but wake on first press and run a fast 10 ms window (existing code already polls at 10 ms while pressed) — recommended first try: a press while idle already switches to 10 ms polling, so a second tap within ~500 ms is likely catchable. Verify on bench; fall back to 2.

### Proposed idle-gesture state machine (idle only; disabled if recording/exclusive/faulted/USB command session)

```
IDLE_ARMED --press(debounced)--> PRESS1
PRESS1 --release <500 ms--> GAP        (hold >=3 s -> existing START, no BLE)
GAP --press within 600 ms--> PRESS2
GAP --timeout 600 ms--> IDLE_ARMED     (single tap ignored, as today)
PRESS2 --release <500 ms--> BLE_REQUEST
PRESS2 --hold >=3 s--> existing START behaviour (BLE not entered)
BLE_REQUEST --guards ok--> BLE_MODE ; guards fail--> LED refuse, IDLE
```
Guards: not recording, `!Tcb3Session::exclusive()`, not faulted, battery not critical (if gauge available), USB serial command not active. Parameters (600 ms gap, 500 ms tap) are proposals and need approval/bench tuning.

### BLE lifecycle
```
IDLE --double-tap--> BLE_WAKE (leaveStandby; flash released)
BLE_WAKE --> ADVERTISING (<=180 s)
ADVERTISING --timeout--> BLE_OFF --> IDLE (standby after the usual 2 s)
ADVERTISING --connect--> CONNECTED (no 180 s limit)
CONNECTED --disconnect--> BLE_OFF --> IDLE
CONNECTED --inactivity timeout [needs approval]--> disconnect --> BLE_OFF
Any state --fault/USB-serial command--> BLE_OFF
```
Download is read-only; no delete opcode; nothing is erased after download.

### Mutual exclusion with recording
- A single `BleMode::active()` flag (set before SD/advertising starts, cleared after teardown) is checked in every recording start path: button Start, `Tcb3Session::startFromButton()`, `TCB START`, `RECORD START`, COMP/OBS paths. It is also exposed to `Standby::Inputs` as a wake-lock (`bleActive`).
- Start-hold during BLE: **recommended: refuse with the existing 3× red "start refused" pattern and keep BLE running** (alternative: exit BLE then start — needs decision). Recording never starts while BLE is up.
- Double-tap during recording: ignored (no state change, no LED).
- Existing Start/Stop behaviour is unchanged when BLE is not active.

### LED feedback (single RGB, one colour at a time; existing patterns unchanged)
Existing: power-on 3× blue 150/150 ms; recording solid green; refused 3× red; fault 3× red/2 s; idle off.
Proposed (need approval; BLE patterns must not resemble solid green or red):
- Entering BLE (accepted): 2 long blue flashes (300/200 ms).
- Advertising: short blue blip ~50 ms every 2 s.
- Connected: double blue blip every 3 s (or slow 1 s blue pulse every 3 s).
- Transfer active: faster blue (50 ms every 500 ms).
- Timeout / disconnect: 1 red flash 200 ms then off.
- Refused (guards fail): existing 3× red.
LED current counts against power; blips keep duty <5 % [E].

## 5. Implementation readiness

Safest library/integration: Bluefruit52Lib (the framework's bundled stack) in a separate env; one peripheral service; no central role, no bonding, no pairing in v1; fixed ATT MTU 247 if supported; conn interval 15–30 ms; notifications only from a streamer task with bounded work per wake; flash reads performed by the loop task only (single SPI owner), handing ≤256 B pages to a ring buffer; no BLE calls from recording code paths; read-only session access; app owns the import ledger.

Prerequisites before Phase 1 (approval needed):
1. Reproduce the real build (network access to Seeed host, or CI) and record `pio run` size for default env as the golden baseline.
2. Hardware baseline measurements in §2/§3 on a sacrificial or supervised pod.
3. Decide: SD stays enabled-idle vs teardown; standby polling/GPIOTE change; connected-inactivity timeout; start-hold-during-BLE behaviour; LED patterns; double-tap timing.
4. Reconcile `origin/standby-power-optimisation`.
5. Confirm the Seeed core task priorities and that `loop()` busy-spin does not starve BLE tasks (and vice versa).
6. Add a minimal timing instrumentation build (counters only) to measure loop gap and IMU lateness.

## 6. Test results in this sandbox

`python tests/reusable/run_all_tests.py` with the pinned Python venv: **6 PASS / 28 FAIL**, all failures due to missing `arm-none-eabi-g++` (the toolchain downloads from `files.seeedstudio.com`, unreachable here); passing suites include `button_tests`, `standby_tests`, `battery_gauge_tests`. Failures are environmental, not firmware regressions; CI (`.github/workflows/firmware-build.yml`) is the authoritative run. No firmware source was modified.

## 7. Acceptance criteria for preserving recording reliability

With BLE-capable firmware, BLE never entered, vs default build baseline on real hardware:
- Zero missed/rejected/full/CRC/incomplete-write counters; no queue overflow; IMU mean 100.00±0.01 Hz, σ and max error not worse than 10 % over the baseline (σ ≤ ~460 µs, max error ≤ ~1.1 ms); GPS RMC 10 Hz epoch counts equal, no UART overrun.
- Recorded sessions byte-compatible; flash image hash of untouched sessions unchanged after BLE transfers; no writes to sessions during BLE.
- Default firmware binary hash unchanged (BLE only in the separate env).
- Double-tap during recording: no BLE start, no state or LED change; BLE active: recording start always refused.
- Standby current regression ≤ an agreed limit (to be set after baseline measurement) and BLE returns to existing standby within 5 s of timeout/disconnect.
- 100 enter/exit BLE cycles without leak (heap, tasks, standby wake-lock) and without missed recording starts afterwards.
- Download interrupted at random points never alters stored data; resumption completes with matching CRC.

## 8. Items requiring approval / blockers

1. Obtaining a working build/toolchain here (blocked by network) or authorising CI/bench builds.
2. Standby poll rate or GPIOTE change for double-tap.
3. SoftDevice teardown vs enabled-idle policy.
4. Connected-inactivity timeout (the requirement says connections persist; propose e.g. 5 min with no GATT activity — needs agreement).
5. Behaviour for start-hold during BLE; LED patterns; tap/gap timings.
6. Hardware measurements and flashing (not authorised yet).
