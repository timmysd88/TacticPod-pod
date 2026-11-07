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

## 4. Button-driven Bluetooth Mode (rev 1 — SUPERSEDED by §9 where they differ)

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

---

# 9. Revision 2 — approved design decisions and verification (investigation only)

Approved: GPIO-interrupt idle double-tap; 500 ms window; 30–50 ms debounce; 180 s advertising; 5 min connected-inactivity (not interrupting healthy downloads); 3 s hold while advertising cancels BLE and starts recording; 3 s hold while connected ignored; LEDs: advertising = blue flash/2 s, connected = 2 blue flashes/5 s, otherwise off; recording and BLE mutually exclusive; sessions never erased. No firmware change, flash or implementation has been done. Evidence labels as above; upstream Adafruit core is used as a proxy for the Seeed fork **[?] until verified on the real framework**.

## 9.1 Verified facts and technical conflicts (report before proceeding)

| # | Finding | Impact |
|---|---|---|
| C1 | Core `attachInterrupt()` (upstream `WInterrupts.c`) uses **GPIOTE IN-event channels** (NVIC prio 3), not the low-power PORT/SENSE event. Pin is already `INPUT_PULLUP`. | IN-event channels may add standby current [?]. Use a pod-owned SENSE/PORT configuration (`nrf_gpio_cfg_sense_input` + GPIOTE PORT event) instead; verify on bench. GPIOTE prio 3 is allowed with SoftDevice (reserved: 0,1,4). |
| C2 | Standby currently wakes every 50 ms via `delay(50)` (tickless RTOS) and `standbyIdle()` also runs `BatteryRuntime::update(true)` at that cadence. | An interrupt only lowers idle power if the poll interval is **lengthened**. That depends on battery-monitor cadence requirements (not yet reviewed) → needs approval and measurement. If not lengthened, the interrupt gives accuracy (no missed taps), not power savings. |
| C3 | Debounce is 25 ms today (`kDebounceMs`), approved range is 30–50 ms. | Changing it globally alters existing tested start/stop behaviour (hold/tap tests assume 25 ms). Proposal: keep 25 ms for existing paths, use **40 ms only for the new idle-tap classifier**. Needs approval; otherwise change the constant and re-run button tests. |
| C4 | Pod never calls `Bluefruit.begin()` (grep: no Bluefruit/SoftDevice/InternalFS use in `src/`). SoftDevice is only enabled by `Bluefruit.begin()` (`sd_softdevice_enable`). | Default builds run with SD **disabled** — no SD cost today. BLE is a pure addition if begin() is only called on entering BLE Mode. |
| C5 | Bluefruit has **no `end()`**. `begin()` creates BLE and SOC RTOS tasks, semaphores and a timer with no deletion path; calling `begin()` twice would leak/duplicate them. `sd_softdevice_disable()` exists in the SD API but Bluefruit state and tasks would be left dangling. | Full SD teardown inside the running firmware is **not safely supported** without patching the framework. See 9.2. |
| C6 | `begin()` calls `bond_init()` (InternalFS). | Must confirm on bench that nothing is written to internal flash with no bonding (CPU stall ~85 ms [E] if it does). |
| C7 | Three-second hold while connected must be swallowed; `PhysicalButton` returns `Action::Start` once at 3 s. | BLE controller must intercept the Start action while CONNECTED and ignore it. Verify one-shot semantics so release does not retrigger. |
| C8 | Single LED, one colour at a time (`Color{Off,Blue,Green,Red}`); existing 3×blue 150/150 ms power-on cue. | Both blue patterns are **possible with existing hardware and scheduler** (periodic pattern like the existing `repeatingRed`). Must be distinct from power-on (3×150 ms contiguous): use 100 ms flashes. Needs a new scheduler input (`bleState`) with priority fault > one-shot > BLE > recording > off; production-file change, deferred to implementation. |
| C9 | Wake path: BLE flash reads need `leaveStandby()` first (clock, flash release, GPS resume, IMU init). | BLE Mode entry must call it; GPS resumes during BLE (power cost) — consider a BLE-only wake that skips GPS/IMU re-init. **Do not change GPS behaviour without approval.** |
| C10 | USB present keeps pod awake today. | Decision needed: allow BLE with USB attached (proposal: refuse; USB `TCB READ` already exists). |
| C11 | Build cannot be reproduced here (`files.seeedstudio.com` unreachable); 28/34 host suites need the ARM toolchain. | **Blocks all RAM/flash/SD-config verification and any implementation.** |

No blocker conflicts with the approved decisions, but C2, C3, C5 and C11 need your decision/action.

## 9.2 SoftDevice lifecycle: is full teardown needed?

Options:
- **A. Lazy `Bluefruit.begin()` on first BLE entry; after BLE, stop advertising/disconnect and leave SD enabled until next reset.** Simple, but afterwards the firmware records with SD enabled (SD-reserved peripherals/priorities, SD-managed sleep/clock). Not the tested recording configuration. Risk: recording behaviour differs after a BLE session.
- **B. Patch/teardown (`sd_softdevice_disable` + task deletion + re-begin).** Requires modifying the Seeed framework fork; fragile; rejected for v1.
- **C. (Recommended, pending bench) Soft reset after BLE ends (`NVIC_SystemReset`)**: BLE Mode runs, then reset returns the pod to exactly the existing boot→standby state with SD disabled. No leaks, no residual SD state, recording always runs in the tested configuration; stored sessions are untouched (reset epoch handling already exists). Costs: a reboot (~seconds), USB re-enumeration, boot LED cue must be suppressed (retained-RAM flag or `RESETREAS`) so the power-on 3× blue does not appear after BLE, and a Start-hold during advertising can start recording without reset (SD only entered once advertising began, so reset first, then start — see 9.5). Needs approval and verification of the boot path.

Benefit of teardown: avoids SD overhead (RTC/clock, event wakeups) during recording and idle; C achieves this fully. Recommendation: **C**, with A as fallback if the reset-based path is rejected.

## 9.3 Button state machine (idle only)

Input: GPIO sense interrupt records timestamped edges (ISR does no logic: store `micros`/level in a small ring + wake flag); classification runs in loop/standbyIdle.
Parameters: debounce 40 ms (idle classifier), tap ≤ 500 ms press length, **second press must begin ≤ 500 ms after first release**, hold 3 s unchanged.

```
IDLE --press stable 40ms--> PRESS1
PRESS1 --release <=500ms--> GAP     (held >=3 s -> existing Start, unchanged)
GAP --press within 500ms--> PRESS2
GAP --500ms timeout--> IDLE         (single tap ignored, as today)
PRESS2 --release <=500ms--> BLE_REQUEST
PRESS2 --held >=3 s--> existing Start behaviour (no BLE)
BLE_REQUEST: guards ok --> BLE_MODE; guards fail (recording/exclusive/faulted/USB) --> ignore, no LED change
```
Disabled entirely while recording, so the existing tap + 3 s hold stop gesture is unaffected. In standby the first edge wakes the loop; existing code already polls at 10 ms while pressed. Spurious wake mitigation: SENSE latch only, discard events not confirmed after the 40 ms debounce, no state change on noise, count of unconfirmed wakes exposed in `FieldTrace` for the power test.

## 9.4 BLE lifecycle and timers

```
STANDBY --double-tap--> WAKE --> ADVERTISING (<=180 s)
ADVERTISING --180 s, no connection--> BLE_STOP --> existing standby
ADVERTISING --3 s hold--> BLE_STOP (clean) --> existing recording start workflow
ADVERTISING --connect--> CONNECTED (no 180 s limit)
CONNECTED --3 s hold--> ignored
CONNECTED --disconnect--> BLE_STOP --> existing standby
CONNECTED --inactivity 5 min--> disconnect --> BLE_STOP --> existing standby
ANY --fault--> BLE_STOP
```
Return-to-standby uses the existing standby policy (2 s settle) with a `bleActive` wake-lock that is released only after BLE_STOP completes; a watchdog cap (e.g. wake-lock auto-release after 10 min of no state change) is recommended so a bug cannot hold the pod awake.

**5-minute timer.** Counted from the last *valid application activity* = a well-formed, in-protocol request from the app (identify, list, transfer request, ACK/window ACK, keep-alive). Link-level traffic, bare connection events or malformed writes do not reset it. The timer is **suspended while a transfer is in the TRANSFERRING state and progress is being made** (window ACK received within a stall limit, proposed 60 s). If a transfer stalls past that limit it is aborted (data untouched) and the 5-minute timer starts from that point. Connection loss is detected by the supervision timeout (4–6 s proposed), independent of this timer. All timers use `millis()` deltas, not wall time.

## 9.5 Recording/BLE mutual exclusion

- `BleMode::active()` (true from BLE_WAKE until BLE_STOP completes) is checked in **every** start path: button Start, `Tcb3Session::startFromButton()`, `TCB START`, `RECORD START`, COMP/OBS paths; and BLE entry requires `!recording && !exclusive() && !faulted`. It is also a standby wake-lock input.
- Hold during ADVERTISING: BLE_STOP completes (adv stop, no flash access in flight, `active()` false; with option C also a reset) **then** the existing `startFromButton()` runs; the LED shows the existing recording/refused pattern. If the BLE stop fails or exceeds a short bound (e.g. 500 ms), refuse start with 3× red rather than start while BLE is up.
- Hold during CONNECTED: ignored.
- BLE flash reads are read-only; no erase/write opcode exists; no erase after download.

## 9.6 LED proposal (confirmed possible with existing hardware)

| State | Pattern |
|---|---|
| Advertising | blue 100 ms flash every 2 s |
| Connected | two blue 100 ms flashes (100 ms gap) every 5 s |
| BLE inactive / standby | off |
| Power-on, recording, refused, fault | unchanged |
Duty < 10 % (advertising 5 %, connected ~6 %); estimated LED power cost is small but included in the advertising/connected current measurements.

## 9.7 Revised implementation sequence (nothing started)

0. **Gate (blocks everything):** working build environment (Seeed host access or CI), golden default-env size/hash, bench measurements per §3, decisions C2/C3/C10 and 9.2 option.
1. Host-only prerequisites (can be developed and tested without hardware, tests in the existing harness, no firmware behaviour change): extract idle-gesture classifier and `BleMode` interlock as pure headers with host tests (`tests/reusable/ble_gesture_tests.py`), plus LED scheduler pattern tests. Not wired into `main.cpp`.
2. Bench-only spike on a sacrificial pod (needs separate flash approval): GPIO SENSE wake current vs poll, SD begin RAM, option C reset path, loop jitter with BLE tasks idle.
3. Phase 1 (separate env `tacticpod_reusable_ble`, default envs byte-identical): gesture wiring, BleMode, advertising/timeouts, identity/battery, LEDs.
4. Phases 2–5 as in the round-1 plan (session list, binary transfer, app, validation) with the timers in 9.4.

Additional acceptance criteria: default-env binary hash unchanged; idle standby current with the interrupt ≤ baseline (limit set after measurement) and zero unconfirmed wakes over 24 h; 500 double-tap/hold/single-tap/triple-tap trials classified correctly with no accidental start; no BLE entry during recording across 100 attempts; no recording start while BLE active across 100 attempts; hold-in-advertising starts recording within an agreed bound; hold-in-connection never interrupts a transfer; healthy 90-minute-class download is never cut by the 5-min timer; disconnect returns to standby within 5 s; flash image hash of sessions unchanged after every scenario.

## 9.8 Needs your approval before proceeding

1. C2: lengthening the standby poll interval (battery-monitor cadence) to realise interrupt power savings, or accept interrupt for accuracy only.
2. C3: 40 ms debounce for the new idle classifier only (vs global change).
3. 9.2: option C (reset after BLE) vs A.
4. C10: refuse BLE while USB is attached.
5. Stall limit (60 s), supervision timeout, wake-lock safety cap.
6. Build-environment access, bench measurements and flashing.

---

# 10. Revision 3 — decisions applied, Seeed core verified, build/test status (investigation only)

## 10.1 Approved decisions (rev 3)

Keep 50 ms standby polling and battery monitoring unchanged until measured; 40 ms debounce for the new idle double-tap only (existing 25 ms recording logic untouched); evaluate PORT/SENSE (not GPIOTE IN); conditional approval of soft reset after BLE; BLE allowed with USB attached (no blanket restriction); 60 s stalled-transfer timeout reset **only** by valid transfer progress; 5 min connected inactivity (not applied to healthy downloads); 180 s advertising; recording and BLE mutually exclusive; LEDs and recording unchanged outside BLE. Supersedes §9.8 and the USB refusal in §9.1 C10/§9.3 guards.

## 10.2 Verified against the real Seeed core (not the Adafruit proxy)

Source: `Seeed-Studio/Adafruit_nRF52_Arduino` from GitHub. The pinned package `framework-arduinoadafruitnrf52-seeed 1.10101.0` is only on `files.seeedstudio.com` (unreachable). The pod's `variants/tacticpod/.../variant.h` is byte-identical to the core at tags v1.1.3 … 1.1.13, so **v1.1.3 was used as the nearest verifiable match [?]**; the exact pinned tarball remains unverified.

| Item | Verified result (core v1.1.3) |
|---|---|
| `attachInterrupt` | Uses GPIOTE **IN-event** channels (CONFIG mode Event), NVIC prio 3. **Confirmed not low-power PORT.** The pod itself uses no `attachInterrupt`/GPIOTE, nor do Bluefruit/TinyUSB libs. |
| `GPIOTE_IRQHandler` (core) | Services only IN channels; it **never clears `EVENTS_PORT`**. Enabling `INTENSET.PORT` while the core handler is linked would cause an interrupt storm. |
| SENSE support | `pinMode(pin, INPUT_PULLUP_SENSE)` exists (SENSE=Low), so DETECT can be armed with core APIs only. |
| Sleep path | `wiring.c`/FreeRTOS tickless use `sd_app_evt_wait()` if SD enabled, else `WFE/SEV/WFE`; already SD-aware. RTOS tick (≤50 ms via `delay(50)`) wakes standby regardless of any pin interrupt. |
| SoftDevice | Board option is **S140 7.3.0** (fwid 0x0123) with `nrf52840_s140_v7.ld`: FLASH origin 0x27000, RAM origin 0x20006000. SD RAM block therefore fixed at 24 KB; SD flash not counted in app flash. |
| Teardown | No `Bluefruit.end()` and no `sd_softdevice_disable` call anywhere in core/Bluefruit. `begin()` creates BLE/SOC tasks and a timer with no deletion path. **Confirms full teardown is unsupported** → reset-based return (§10.5). |
| `bond_init()` | Runs `InternalFS.begin()` and `mkdir` of the bond dirs if missing, so the **first ever `Bluefruit.begin()` writes (and possibly formats) internal flash** (CPU stalls tens–hundreds of ms [E]). Safe only because BLE never runs while recording; must be bench-checked, and it writes internal flash only, not the SPI session flash. |
| Task priorities | loop = 1 (LOW), timer/callback = 2, BLE/SOC = 3 (HIGH). Unchanged from the proxy. |

### GPIO PORT/SENSE conclusion
- With 50 ms polling retained (approved), a pin interrupt adds **no power benefit** (the tick already wakes every 50 ms) and a PORT interrupt cannot be enabled safely while the core's `GPIOTE_IRQHandler` is linked.
- **Recommended Phase 1: no interrupt.** Detect the first press with the existing 50 ms standby poll, then run 10 ms polling for the 500 ms gesture window (the existing code already drops to 10 ms while pressed; extend that window in the idle classifier only). 40 ms debounce means taps shorter than 40 ms are rejected anyway; the residual risk is a 40–50 ms first tap falling between polls, to be measured on hardware.
- **Optional spike (needs flash approval):** `INPUT_PULLUP_SENSE` + GPIOTE PORT with NVIC left disabled and `SEVONPEND`, or a project-owned `GPIOTE_IRQHandler` (legal only if `WInterrupts.o` is never linked — check the link map). Evaluate only if measurements show polling is insufficient. Wake on SENSE must be measured for spurious DETECT events (button bounce, pull-up leakage) over 24 h.

## 10.3 Build environment and regression suite

- **PlatformIO full build: NOT restored.** `files.seeedstudio.com` and the PlatformIO registry (`registry.platformio.org`, `dl.registry.platformio.org`, `api.registry.platformio.org`) are unreachable; only GitHub, PyPI and the OS apt mirrors resolve. The pinned platform (commit a6fd41d) installs from GitHub, but `pio run` still fetches `esp_install` and other packages from Seeed/registry hosts and aborts. The pinned framework tarball is unavailable, so **no firmware ELF, no real size/RAM figures and no SD RAM check were produced.**
- **Host regression environment restored (outside the repo, in git-ignored `.pio-core/`):** apt `gcc-arm-none-eabi` 13.2.1 (the pinned PlatformIO toolchain is GCC 7.x), wrapper `arm-none-eabi-g++.exe` adding `--specs=nosys.specs`, Seeed core v1.1.3 as the framework package, venv with unicorn 2.1.4 and pyelftools. **No tests or sources were modified.**
- **Result of `tests/reusable/run_all_tests.py`: 33 of 34 suites PASS.** The one failure, `tests/compact/test_resources.py`, requires `.pio/build/seeed_xiao_nrf52840_sense/firmware.elf` from a real PlatformIO build.
- Caveats: GCC 13 vs the pinned GCC 7 and core v1.1.3 vs the pinned build mean pass/fail logic is meaningful but any size/stack figures from this environment are **not** authoritative. CI (`firmware-build.yml`, pinned PlatformIO 6.2.0 and platform commit) remains the authoritative run. Passing suites are ARM-code-in-Unicorn simulations; they prove logic, not hardware timing.

## 10.4 Which tests need physical hardware

Host/Unicorn (can run without hardware, already in the suite or addable): button FSM, standby policy, TCB3 store/export, LED scheduler patterns, BLE gesture classifier and `BleMode` interlock (new), timers (60 s stall, 5 min inactivity, 180 s adv) with a fake clock.
**Hardware required:** firmware size/RAM from the real pinned build (CI or bench), `Bluefruit.begin()` RAM log, IMU/GPS timing with BLE-capable build, loop jitter during BLE (BLE tasks pre-empting loop), standby/advertising/connected/download current, 40–50 ms tap vs 50 ms polling reliability, SENSE spurious-wake rate, reset-path behaviour (all items in §10.5), USB re-enumeration after reset, real throughput/MTU, connection-drop recovery, Web Bluetooth end to end.

## 10.5 Soft reset after BLE: exact behaviour and proof obligations

Reset is `NVIC_SystemReset()` performed only after BLE_STOP; no code path resets during recording.

Boot facts verified in source (`src/main.cpp` `setup()`, `flash_power.h`, `docs/fast-record-reset.md`):
- Flash is **always released from deep power-down first** at boot (reset during standby is already handled).
- `Recorder::begin`/`Tcb3Session::begin` is a **read-only recovery scan; recording stays OFF**; nothing starts recording at boot. Button `armed` is false until release, so a held button cannot start a recording.
- The 3× blue PowerOn cue plays after a successful boot, so a post-BLE reset would show it unless suppressed; suppression needs a retained flag (production change, requires approval).
- BLE is read-only on the SPI flash (no erase/write opcode), so nothing can be mid-write at reset; any export read is simply abandoned. The pod never writes during BLE, so "pending flash operations" reduces to: ensure the BLE flash-read slice (≤64 B SPI transaction) has completed before reset, and do not enter deep power-down mid-transaction. The pod re-enters standby via the existing path after boot.

State/transition table:

| From | Event | Actions | To |
|---|---|---|---|
| STANDBY | double-tap (guards: not recording/exclusive/faulted; USB allowed) | leaveStandby, `BleMode::active=true`, `Bluefruit.begin()` (first time per boot), start adv | ADVERTISING |
| ADVERTISING | 180 s, no connection | stop adv, drain, `BleMode` BLE_STOP | RESET |
| ADVERTISING | connect | stop adv timer | CONNECTED |
| ADVERTISING | 3 s hold | stop adv, BLE_STOP; then see conflict C12 | RESET (+ start per C12) |
| ADVERTISING | double-tap | ignored | ADVERTISING |
| CONNECTED | list/read/ACK/keepalive request | refresh 5 min activity timer | CONNECTED |
| CONNECTED (transferring) | valid progress (window ACK) | reset 60 s stall timer; 5 min timer suspended | CONNECTED |
| CONNECTED (transferring) | 60 s without valid progress | abort transfer (data untouched), start 5 min timer | CONNECTED |
| CONNECTED | 5 min since last valid application activity (not transferring) | disconnect | BLE_STOP |
| CONNECTED | link drop / supervision timeout / app disconnect | abort transfer, BLE_STOP | RESET |
| CONNECTED | 3 s hold | ignored | CONNECTED |
| any BLE state | fault | BLE_STOP | RESET |
| RESET | boot | existing boot path, recording OFF, flash released, standby after settle | STANDBY |

Proof obligations for the conditional approval (all hardware-dependent unless noted):
1. No recording erased/corrupted: SPI flash image hash (e.g. `TCB READ` export CRC) identical before and after 100 BLE cycles with reset (hardware), plus host proof that BLE code has no write/erase path (code review + host test).
2. No auto-start after reset: boot test asserts `recording()==false`, no start attempt, across all exit paths including hold-during-advertising (host + hardware).
3. Returns to the existing standby: measured entry within the existing 2 s settle + 5 s bound; standby current equals baseline (hardware).
4. Pending flash ops complete: assert no in-flight SPI transaction at reset (host fake + hardware logic trace).
5. Disconnection, advertising timeout, failed/stalled transfers each end in RESET→STANDBY; fault-injection tests for each (host fake clock + hardware).
6. Verify the Adafruit bootloader double-reset DFU detection is not triggered by BLE reset followed by an immediate user reset, and that no GPREGRET magic used for DFU is set [?].

### Conflict C12 (needs your decision)
Approved item 7 says a 3 s hold during advertising cancels BLE and **starts the recording workflow**, while the reset condition says no recording may start automatically after a reset. Options:
- **A (recommended):** BLE_STOP, reset, then the user holds again (button `armed` rule; one extra hold, LED shows normal boot cue). Fully preserves "reset never starts recording"; deviates from item 7 by requiring a second hold.
- **B:** BLE_STOP then start recording in the same boot without reset (SD stays enabled for that boot; differs from the tested recording configuration; weakens the benefit of reset).
- **C:** BLE_STOP, reset carrying a one-shot retained-RAM "user-requested start" flag (set only by the user's physical 3 s hold), consumed once after boot. Meets item 7 but is a start after reset; the retention register must avoid bootloader DFU magic values.

## 10.6 Interactions confirmed with approved decisions
- **Stalled-transfer timer (60 s):** reset only on valid progress (in-order window ACK with correct CRC/offset); retransmitted/duplicate ACKs, keepalives and non-transfer writes do not reset it. On expiry: abort transfer (session untouched), then the 5 min timer runs.
- **5 min inactivity:** last *valid application activity* only; suspended while a transfer is healthy.
- **USB attached:** BLE allowed; no blanket restriction. USB `TCB READ`/serial commands and BLE reads share the single SPI owner, so a pod-side mutex (`exclusive()`) must make a USB export and a BLE transfer mutually exclusive (reject whichever starts second) — new requirement. Reset after BLE with USB attached re-enumerates CDC (development inconvenience only).
- **Recording interlock, LED patterns (advertising: blue 100 ms flash / 2 s; connected: 2 blue 100 ms flashes / 5 s; idle off):** unchanged from §9.5/§9.6; confirmed feasible with the existing single RGB LED scheduler (one colour at a time, no timing resource conflicts).

## 10.7 Remaining blockers and approvals
1. **Build environment:** no working PlatformIO build here. Options: (a) run the existing CI workflow on this branch (needs approval to push the branch), (b) provide a mirror/network access for `files.seeedstudio.com` and the PlatformIO registry, (c) a developer machine/bench build. Until one is done, no size/RAM figures and no `test_resources` result.
2. Exact pinned framework (1.10101.0) unverified; v1.1.3 used as proxy.
3. Decide C12 (A/B/C).
4. Approve the post-BLE boot-cue suppression via retained flag (if desired) and the pod-side USB/BLE export mutex.
5. Hardware measurements/flashing (not approved yet).
No BLE implementation, flashing or production firmware change has been done.
