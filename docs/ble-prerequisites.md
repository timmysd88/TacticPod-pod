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

---

# 11. Revision 4 — conditions applied, CI review, internal-flash analysis, bench plan, Phase 1 readiness (investigation only)

No firmware, flash, CI trigger, push or stored-session change was made.

## 11.1 Approved conditions applied (supersede earlier rev where different)

- Button: keep 50 ms standby poll; 10 ms poll during the idle double-tap window; **no GPIOTE/PORT interrupts** (the §10.2 spike option is withdrawn); 40 ms debounce for the idle classifier only; existing recording button logic untouched.
- **C12 resolved (option A):** 3 s hold during ADVERTISING → cancel advertising → safe reset sequence → idle; **no recording starts**; a separate 3 s hold is then required. Hold while CONNECTED/transferring is ignored.
- Lifecycle: 180 s advertising; 5 min inactivity (suspended for healthy transfer); 60 s stall (reset only by valid progress); **every BLE exit — advertising timeout, hold-cancel, normal disconnection, link loss, fault — returns to idle through the safe reset sequence.** Recording and BLE remain mutually exclusive.

Updated transition rows (replace those in §10.5): `ADVERTISING --3 s hold--> BLE_STOP --> RESET --> STANDBY (recording OFF; new hold needed)`; `CONNECTED --disconnect/link loss--> BLE_STOP --> RESET --> STANDBY`. The retained-RAM start flag (old option C) and same-boot start (option B) are **not** to be implemented, which also removes the need for a retention register and any DFU-magic concern for GPREGRET.

Safe reset sequence (specification; no implementation): (1) mark `BleMode` stopping, reject all new GATT requests; (2) abort any transfer and wait for the in-flight SPI read slice (≤64 B) to finish, assert no flash command pending; (3) stop advertising / disconnect (bounded wait ≤1 s); (4) LEDs off; (5) `NVIC_SystemReset()`. A watchdog-style bound ensures reset still happens if a step hangs. Boot path is the existing one (flash released, read-only scan, recording OFF, `armed` false until release).

## 11.2 First-tap verification against 50 ms standby polling

Verified in `src/main.cpp` (`loop()`, `updateFieldControls`, `standbyIdle`) and `src/standby_policy.h`:
- In standby the loop still runs `updateFieldControls()` each pass, then `standbyIdle()` sleeps `delay(50)` (or `delay(10)` while D0 is LOW). A button press does **not** by itself leave standby (exit happens only via `mustStayAwake`; `buttonBusy` only delays *entering* standby). So **both taps occur while in standby**, sampled by the polling loop.
- Therefore the first tap can be missed. A host model (period 51 ms, 10 ms samples after first detection, press must stay LOW 40 ms after first detection; uniform random phase) gives the probability that a single tap is detected:

| Press length | 40 | 50 | 60 | 70 | 80 | 90 | ≥100 ms |
|---|---|---|---|---|---|---|---|
| Detected (debounce 40 ms) | 0 % | 18 % | 38 % | 57 % | 77 % | 96 % | 100 % |
| Detected (existing 25 ms, for reference) | 18 % | 38 % | 58 % | 77 % | 97 % | 100 % | 100 % |

  Model assumptions (period, loop overhead incl. `BatteryRuntime::update`) are **estimates**; real period must be measured.
- **Conclusion: with 50 ms standby polling and 40 ms debounce counted from first detection, taps shorter than ~100 ms can be missed; a miss rate of zero cannot be guaranteed by design.** The second tap is safe because the 10 ms window starts at first detection.
- This conflicts with the demand that the first tap cannot be missed. Options **needing your approval** (none implemented): (a) accept and document "press firmly ≥100 ms", validating on hardware with a human tap-duration test (≥200 taps, several users; target ≥99 % first-tap detection); (b) count the 40 ms debounce **from the second LOW sample at 10 ms** rather than from first detection, i.e. classify a press when two consecutive samples are LOW (min guaranteed press ≈60 ms), reducing the miss region while keeping 40 ms for release/chatter; (c) a 20–25 ms standby poll only (excluded by your current condition). Recommendation: (b)+(a); fall back to (c) only if measurement fails.

## 11.3 CI workflow review (`.github/workflows/firmware-build.yml`) — not triggered

- Triggers: `pull_request` to `main` and `workflow_dispatch`. A plain branch push triggers nothing; CI would start by opening a PR to `main` or `gh workflow run firmware-build.yml --ref feature/ble-prerequisites` (the workflow already exists on `main`).
- `permissions: contents: read`; no secrets used; no deploy, release, publish or write steps; the only side effect is a workflow run on a Windows runner (≤60 min) and a 30-day artifact `tacticpod-firmware-<sha>` containing `.hex/.zip/.elf`, logs, `size.txt` and SHA256SUMS (visible to anyone with repo read/Actions access).
- Concurrency group is per ref with cancel-in-progress (cannot cancel runs on other branches).
- Steps give exactly what is needed: default-env build (creates the `firmware.elf` that `test_resources.py` reads), `tacticpod_reusable_usb` build, full regression, `size.txt`, and `pio pkg list -g` (records resolved package versions).
- Reproducibility gaps to review: the toolchain is a range (`>=1.60301.0,<1.80000.0`, preferred `~1.70201.0` = GCC 7.2.1) and the framework is an **unversioned tarball URL** (`files.seeedstudio.com/.../framework-arduinoadafruitnrf52.tar.bz2`), so the framework is not content-pinned; earlier repo docs recorded 1.10101.0. After a run, compare `pio pkg list -g` to 1.10101.0 and record the SHA-256 of the build artefacts; if different, record the delta before accepting baselines.
- A docs-only branch run would build the same firmware as `main` plus docs, giving the golden default-env baseline. Approval needed to push the branch (to `timmysd88/TacticPod-pod`, origin) and to dispatch/open a PR.
- Without CI: the real PlatformIO build is still impossible here (registry and Seeed hosts unreachable); 33/34 host suites pass; `test_resources` and actual RAM/flash remain **unreported until CI or a bench build runs**. Latest repo-documented figures (not from this session): RAM 122,040 B (51.4 %) and flash 202,320 B (24.9 %).

## 11.4 First-time SoftDevice internal-flash writes (verified in Seeed core v1.1.3)

Facts:
- `Bluefruit.begin()` always ends with `bond_init()` → `InternalFS.begin()`; if LittleFS fails to mount (blank/unformatted region) it **erases all 7 pages** (28 KB) at **0xED000–0xF4000** and formats; then `mkdir` of the peripheral and central bond directories if missing. Later boots mount and only read.
- Region map: app flash ends at 0xED000 (linker `FLASH ORIGIN 0x27000, LENGTH 0xED000-0x27000`; SD S140 7.3.0 and MBR below 0x27000); LittleFS 0xED000–0xF4000; bootloader from 0xF4000 (`flash_nrf5x.c` rejects any write at/above `BOOTLOADER_ADDR`). The pod firmware uses no `InternalFS` today (`src/` grep), so the region should be unused by current firmware, bootloader and recordings (external SPI flash holds recordings).
- Cost: page erase ≈85 ms [E] each, formatting ≈0.6 s of stall plus small writes. With the SoftDevice enabled these go via `sd_flash_*`; they may be delayed while the radio is busy, but at first BLE entry the radio is idle. With the SD disabled they use NVMC directly. IMU/GPS/recording are idle in BLE mode, so no recording timing is at risk **provided BLE entry is guarded** (not recording, not exclusive).
Implications / risks:
1. Existing firmware: none while unused; the region becomes non-blank after first BLE entry (state change). Unknown whether any earlier firmware or bootloader version used it — **must be read before the first BLE run** (SWD read, or provision a sacrificial unit first).
2. Bootloader: unaffected (write guard; separate address); DFU of the application does not touch 0xED000+ [?, bootloader source to confirm]. A bootloader/SD change would need re-validation.
3. Recording safety: external-flash recordings are untouched; the one-time stall happens only in BLE entry; power loss during format is recoverable (mount fails → reformat next time).
4. Policy proposal (no bonding): the first BLE entry per pod is a visible side effect of using BLE; to avoid surprises in the field, **provision at the bench** (run a BLE-capable build once on the unit and verify), or accept it at first use with the 0.6 s stall documented. Avoiding InternalFS entirely would require patching the framework (not recommended).
5. Wear: erase cycles occur once per pod (and again only if the FS is corrupted); negligible vs 10k endurance.

## 11.5 Other core findings relevant to Phase 1 (Seeed core v1.1.3, [?] pinned build)
- **Bluefruit drives the blue LED itself:** `bluefruit_blinky_cb` toggles `LED_BLUE` (500 ms) while advertising and solid while connected, unless `Bluefruit.autoConnLed(false)` is called. This would **conflict with the approved LED patterns and the pod's LED scheduler**. Required: call `autoConnLed(false)` before advertising starts and drive the LED only via `StatusLed`.
- LFCLK: `USE_LFXO` (32.768 kHz crystal) in the variant → low-power SD clock; no RC calibration cost.
- DFU magic constants (`0x4e/0x57/0xA8` in GPREGRET) are only set by core `enterSerialDfu/OTA` helpers; the reset sequence must not use GPREGRET.

## 11.6 Bench measurement and validation plan (approved for preparation only; no flashing yet)

Equipment: sacrificial/bench XIAO nRF52840 Sense pod (never a unit holding irreplaceable recordings until validated), Nordic PPK2 or Otii Arc (source-measure at battery terminals, 3.7 V), SWD probe (J-Link/CMSIS-DAP) if available for internal-flash dump, logic analyser (button, LED, SPI CS), Android Chrome phone + desktop Chrome with Web Bluetooth, nRF Connect app, scripted tap rig or solenoid (optional).

Safety preconditions for every bench session: (1) full external-flash export (`TCB READ`) and SHA-256 recorded before and after; (2) no `RECORD ERASE`/`RECORD RESET` commands issued; (3) internal flash 0xED000–0xF4000 dumped (SWD) before first BLE build, or use a unit proven disposable; (4) known-good default firmware image retained for restore (restore flash only with approval).

| # | Test | Method | Pass criterion |
|---|---|---|---|
| B1 | Baseline standby current (default build) | PPK2, 10 min, 50 ms poll | recorded; defines regression limit |
| B2 | Baseline recording timing | 3×≥15 min sessions with default build | IMU/GPS/counters as 3B.10 baseline; zero missed/rejected/CRC |
| B3 | Poll period, first-tap detection | logic analyser on D0 + tap rig/human 200+ taps, 40–150 ms | measured period; ≥99 % first-tap detection (or option b/a decision) |
| B4 | Idle gesture matrix | single tap, double tap, triple, tap+hold, hold; during recording | double-tap → BLE only idle; hold → existing start; recording ignores double-tap; no spurious entry over 24 h |
| B5 | Standby current BLE-capable build, BLE never entered | PPK2 | ≤ B1 + agreed limit |
| B6 | First `Bluefruit.begin()` | SWD before/after dump, current trace, serial log | only 0xED000–0xF4000 changes; stall ≤1 s; external flash hash unchanged; RAM requirement log line |
| B7 | Advertising 180 s current | PPK2 | recorded; ends with reset to standby |
| B8 | Idle connection / active transfer current | PPK2, nRF Connect then app | recorded |
| B9 | Reset sequence | 100 cycles each exit path (timeout, hold-cancel, disconnect, link loss, fault inject) | back in standby ≤ agreed bound; recording never auto-starts; flash hash unchanged; heap/tasks clean |
| B10 | Recording with BLE build after BLE use | 3×15 min | timing no worse than B2 |
| B11 | Mutual exclusion | start attempts while BLE; double-taps while recording (100 each) | all refused/ignored, LED per spec |
| B12 | Throughput/MTU/stall/inactivity timers | phone + desktop | MTU, rate, 60 s stall and 5 min timers behave as specified; healthy 90 min-class download not interrupted |
| B13 | USB attached | BLE with USB serial open; concurrent `TCB READ` vs BLE | mutually exclusive, no corruption |
| B14 | Power loss | cut power mid-advertising/transfer/format | no external flash change; boots to standby |

Tests runnable without hardware (host/Unicorn): gesture classifier model, timers with fake clock, `BleMode` interlock, LED scheduler patterns, safe-reset sequencing logic, regression suite.

## 11.7 Phase 1 implementation readiness report

Scope of Phase 1 (once approved): BLE-capable env `tacticpod_reusable_ble` (default envs byte-identical): idle double-tap classifier; `BleMode` state machine and interlocks; advertising ≤180 s; identity/battery service; connection/disconnection; 5 min inactivity; reset sequence; BLE LED patterns; `autoConnLed(false)`. No session listing/download yet.

Files expected to change: `platformio.ini` (new env), new `src/ble_mode.h/.cpp`, `src/ble_gesture.h`, `src/ble_service.cpp`, `src/main.cpp` (hooks in `updateFieldControls`, `standbyIdle`, start paths), `src/standby_policy.h` (BLE wake-lock input), `src/status_led.h` (BLE patterns), `src/tcb3_session.cpp` (start guard), new host tests under `tests/reusable/`, docs.

| Gate | Status | Notes |
|---|---|---|
| Design decisions (button, lifecycle, timers, C12, USB, reset) | **Ready** | all approved; first-tap policy (11.2) needs a choice |
| Host regression | **Ready/partial** | 33/34 pass locally with non-pinned toolchain; `test_resources` pending |
| Real PlatformIO build, firmware ELF, size | **BLOCKED** | needs CI (not yet approved to push/trigger) or networked build |
| Exact pinned core/toolchain | **Unverified** | floating Seeed tarball and toolchain range (11.3) |
| SD RAM requirement/`Bluefruit.begin()` | **Unverified** | needs real build + bench B6 |
| Internal flash 0xED000–0xF4000 state | **Unknown** | read before first BLE run (11.4) |
| First-tap reliability | **Risk (medium)** | cannot be guaranteed by polling alone (11.2) |
| Bluefruit blue-LED conflict | **Known, mitigated** | `autoConnLed(false)` (11.5) |
| Power baselines | **Not measured** | bench B1/B5/B7/B8 |
| Recording timing with BLE build | **Not measured** | bench B2/B10 |
| Hardware | **Not approved to flash** | |

**Recommendation: do not start Phase 1 coding until (1) CI build + regression (incl. `test_resources`) pass and the actual RAM/flash numbers and resolved package versions are recorded, (2) first-tap policy chosen, (3) internal-flash state read/provisioning plan agreed. Host-only groundwork (gesture classifier, `BleMode`, timers, LED patterns with tests, not wired to `main.cpp`) can begin after approval since it cannot change firmware behaviour.**

Complexity/risk: Phase 1 medium. Top risks: BLE tasks pre-empting the busy `loop()` (affects only BLE mode, since BLE never runs while recording), reset-sequence hangs, first-tap misses, one-time internal-flash format, Bluefruit LED takeover.

## 11.8 Approvals needed now
1. Push `feature/ble-prerequisites` to origin and run CI (`workflow_dispatch` or PR) — or an alternative networked build.
2. First-tap policy: 11.2 (a)/(b)/(c).
3. Internal-flash provisioning approach (11.4 item 4) and permission for an SWD read-only dump on a bench unit.
4. Permission to start host-only groundwork code (no firmware wiring).
5. Hardware flashing remains unapproved.

# 12. Revision 5 — CI artifact, resource baseline and internal-flash backup plan

This revision supersedes the pending CI, host-groundwork and first-tap items in §11. The successful run and its artifact were inspected; no CI was rerun, no hardware was accessed, and no flash or recording was changed.

## 12.1 Verified CI build and regression

- Draft PR: `timmysd88/TacticPod-pod#11`; CI run `42385281072`, completed **success** on commit `70a5e9014809bb2dac75851cc479da1fdf60de43` (the PR merge-test commit was `094b6c77cf736a41b48910b2174accc58d64bd72`).
- Both `seeed_xiao_nrf52840_sense` and `tacticpod_reusable_usb` built successfully. All **35/35 regression scripts passed**, including `test_resources` and `ble_groundwork_tests.py`.
- The resource test reports production static RAM **118,996 B / 119,000 B limit** and compact objects **37,966 B / 38,000 B limit**. These checks are host/build gates, not hardware timing measurements.
- The downloaded artifact SHA-256 manifest verified. The reusable ELF SHA-256 is `04f017684694b04f7ecf0c456355da59154203ada7affeae5d2520fb0c1a6510`.

### Measured firmware size (PlatformIO `size` output)

The linker-defined capacities are 237,568 B RAM and 811,008 B application flash.

| CI environment | RAM used | RAM headroom* | Flash used | Flash headroom |
|---|---:|---:|---:|---:|
| `seeed_xiao_nrf52840_sense` (default) | 118,996 B (50.1%) | 118,572 B | 166,220 B (20.5%) | 644,788 B |
| `tacticpod_reusable_usb` | 123,092 B (51.8%) | 114,476 B | 217,464 B (26.8%) | 593,544 B |

`*` Arithmetic against the PlatformIO size report, not a promise that all remaining RAM is available to BLE. The reusable ELF reserves a 112,428 B heap region and 2,048 B stack; BLE/SoftDevice configuration can move the required RAM boundary and must be measured in a BLE-enabled build. The default production resource test is only 4 B below its current static-RAM limit, so BLE must stay in a separate environment unless that policy is explicitly revisited.

**Assessment:** application flash has ample headroom for a Phase 1 advertising/identity implementation. RAM appears plausible for the small Phase 1 scope, but BLE fit is **not verified**: the artifact contains no BLE application, and there is no measured `sd_ble_enable` RAM requirement for the proposed service configuration. Session transfer buffers would need a separate budget and build.

## 12.2 Exact build versions and dependency status

- PlatformIO Core **6.2.0**.
- Seeed platform **1.1.1**, resolved from the workflow-pinned Git commit `a6fd41debbbcda13b5340fff50c9092ed3289194`.
- Arduino nRF52 framework **1.10101.0** (`1.1.1`), matching the intended framework version.
- GNU Arm toolchain **1.70201.0 (GCC 7.2.1)**.
- CMSIS package **2.50700.210515**.
- CI checks out LSM6DS3 commit `e063c3a544b8bb89238accddf24f0028bcd1ca20` and TinyGPSPlus commit `ca29434514a5c5172bd807af0608df7f296582a2`; build metadata identifies LSM6DS3 2.0.7 and TinyGPSPlus 1.0.3.

The platform and library source commits are pinned by the workflow. The framework resolved to the correct version, but is fetched through an unversioned Seeed tarball URL and is not pinned by content hash. The toolchain range also permits other releases; this run resolved to 1.70201.0. These are reproducibility risks, not failures of this CI build.

## 12.3 ELF and internal-flash map assessment

The artifact contains the `tacticpod_reusable_usb` ELF and HEX, but **no linker map file**. The ELF, HEX, build log and the resolved framework linker/flash-driver sources were inspected:

- The selected `nrf52840_s140_v7.ld` declares application FLASH `[0x27000, 0xED000)` and RAM `[0x20006000, 0x20040000)`. The application ELF sections start at `0x27000`; `.text` ends at `0x5B62C`, initialized data is loaded through `0x5C178`, and the HEX payload ends at `0x5C178`. These application payloads are well below `0xED000`.
- The framework's `InternalFileSystem.cpp` assigns LittleFS `[0xED000, 0xF4000)` to seven 4 KiB pages (28 KiB). Its nRF52840 flash driver sets `BOOTLOADER_ADDR` to `0xF4000` and rejects writes at or above that address. Thus the **software layout** has no application/LittleFS overlap; the proposed region ends at the framework's bootloader write boundary.
- Current `src/` does not use `InternalFS`. Recorded sessions are on the separate external SPI flash; the CI ELF contains no proof of the contents of any physical pod's internal flash. The CI artifact does not contain the installed bootloader or its settings-page map. Therefore prior firmware data, a customized bootloader layout, and the actual target's `[0xED000, 0xF4000)` contents remain **unverified**.
- `Bluefruit.begin()` uses `bond_init()` and `InternalFS.begin()`. If LittleFS cannot mount, it formats the filesystem, which can erase/write those seven pages before creating bond directories. This is LittleFS provisioning, not programming the S140 image. The source-level region assignment makes it an intended filesystem area, but does **not** make a first-use format safe for a particular pod without first reading that pod's contents.

**Safety decision:** do not invoke BLE/`Bluefruit.begin()` on a pod until its internal flash has been read and the dump reviewed. Any later first-time format/provisioning still requires explicit approval. No SWD connection or flash operation was performed for this investigation.

## 12.4 Prepared read-only SWD backup procedure (not executed)

**Hardware/software:** an SWD-accessible XIAO nRF52840 Sense; stable target power and a correctly wired SWDIO, SWDCLK, GND and VTref connection; a SEGGER J-Link-compatible probe; Nordic nRF Command Line Tools (`nrfjprog`) and SEGGER J-Link software. Record the board identity, probe serial, tool versions, power source and operator with the output. Do not connect a probe if debug access is locked or the wiring/power arrangement is uncertain.

On a host with `nrfjprog` and `sha256sum`, first identify the probe, then replace `<SERIAL>` with its listed serial number. These commands only read code flash and UICR:

```sh
nrfjprog --version
nrfjprog --ids
nrfjprog --family NRF52 --snr <SERIAL> --readcode internal-flash-1.hex
nrfjprog --family NRF52 --snr <SERIAL> --readuicr uicr-1.hex
nrfjprog --family NRF52 --snr <SERIAL> --memrd 0xED000 --n 28672 > internalfs-region-1.txt
sha256sum internal-flash-1.hex uicr-1.hex internalfs-region-1.txt
nrfjprog --family NRF52 --snr <SERIAL> --readcode internal-flash-2.hex
nrfjprog --family NRF52 --snr <SERIAL> --readuicr uicr-2.hex
nrfjprog --family NRF52 --snr <SERIAL> --memrd 0xED000 --n 28672 > internalfs-region-2.txt
sha256sum internal-flash-2.hex uicr-2.hex internalfs-region-2.txt
```

Compare each `*-1` and `*-2` SHA-256 value. A mismatch means the dump is not verified; stop and investigate rather than proceeding. Retain both reads, hashes, command output, board/probe identity and a protected copy of the files. Confirm the full-flash HEX includes the address range `0xED000–0xF3FFF`; the `--memrd` capture provides a direct readable snapshot of that region. A readback is not a substitute for an independently known-good firmware/bootloader image, and a full internal-flash backup does not contain external SPI recordings.

Do **not** use `--recover`, `--eraseall`, `--program`, `--chiperase`, `--sectorerase`, or any command that writes UICR/flash. If read access is denied, stop: do not unlock or recover, since recovery can mass-erase internal flash. These commands are documented for preparation only and were not run.

If a later approved BLE trial finds only a corrupt LittleFS volume, the framework may format the reserved FS region on a subsequent mount attempt; this loses any pre-existing contents in those pages. If the application fails but the installed bootloader remains functional, recovery may be possible by the normal application-only update path. If the bootloader, SoftDevice or UICR is damaged, recovery requires a verified, board-appropriate image and authorized SWD programming; restoring a different unit's UICR or blindly restoring a full dump is unsafe. The external recording flash is separate, but should still be independently exported and hash-checked before any approved bench trial.

## 12.5 Updated readiness and remaining gates

| Gate | Status | Result / next evidence |
|---|---|---|
| CI build and regression | **PASS** | Both builds; 35/35 scripts; resource test included. |
| Framework/toolchain version | **Verified for this run** | Framework 1.10101.0; toolchain 1.70201.0 (7.2.1); source pinning caveat above. |
| Host-only button/BLE groundwork | **PASS** | CI test passed; code remains isolated and unwired to `main.cpp`. The approved second-10 ms-sample approach is implemented in the classifier. |
| Flash/RAM for BLE | **Plausible, not proven** | Good flash headroom; BLE-enabled target build and SoftDevice RAM negotiation are still required. |
| Internal-flash software map | **No address overlap found** | ELF/linker plus framework map place LittleFS between the app limit and bootloader write boundary. No map file or physical dump is available. |
| Per-pod internal-flash contents | **Unknown / blocker before first BLE entry** | Requires an approved read-only SWD backup and review. |
| Power and real-time recording behavior | **Not measured** | Requires approved bench instrumentation and BLE-enabled hardware tests; host tests do not establish 100 Hz/10 Hz timing or current draw. |
| Firmware BLE integration / hardware flashing | **Not authorized** | Production integration, provisioning and flashing remain separate approval gates. |

**Phase 1 readiness verdict:** the host/build prerequisites are ready to support a separate BLE build, and the measured flash budget is ample. RAM is encouraging but unverified for BLE. Before beginning production firmware integration, obtain explicit approval to wire the approved Phase 1 state machine/services into the firmware and add a separate BLE build environment. Before any board runs that can call `Bluefruit.begin()`, obtain separate authorization for the read-only SWD backup and review of the target's internal flash. Any flash/provisioning operation, firmware upload, or physical validation still requires its own approval. Preserve PR #11 as draft; do not merge.

# 13. Phase 1 implementation — opt-in firmware, no hardware execution

Firmware implementation was authorized after revision 5. The prerequisite report was committed first as `edb6dfe`. Phase 1 is enabled only by `tacticpod_reusable_ble`; the default and `tacticpod_reusable_usb` builds do not include the BLE hooks. No hardware flashing, `Bluefruit.begin()` on a pod, internal-flash provisioning, external-flash changes, app changes, merge or deployment is authorized or performed.

## 13.1 Implemented lifecycle and isolation

- The existing `PhysicalButton` classifier remains unchanged. The portable idle gesture runs beside it only in the BLE build: ordinary standby remains 50 ms, candidate/gesture polling is 10 ms, and the idle release/chatter debounce is 40 ms. Deliberate ~100 ms taps are supported; very short taps remain lossy.
- Entry rejects recording, session/USB export/reset work, other capture modes, GPS diagnostics, flash diagnostics, faults and pending button starts. Before lazy initialization, peripherals enter the existing standby power-down path (or remain asleep if already in standby); the external NOR must have WIP/WEL clear. The GPS UART and IMU stay suspended throughout BLE. USB attachment alone is not an entry restriction.
- `Bluefruit.begin(1, 0)` runs only after an admitted double-tap. There is one peripheral link and no central role or high-throughput configuration. `autoConnLed(false)` leaves LED ownership with the application. Advertising uses the custom service UUID and name, a 100 ms interval, and both stack and portable-controller 180 s deadlines.
- The dedicated BLE foreground loop skips USB command processing, GPS/IMU capture, recorder updates and SPI operations. `TCB START`, `TCB READ` and the button start helper also explicitly reject active BLE. Existing recording start/stop paths remain available only when BLE is off.
- Advertising timeout or a three-second advertising hold stops advertising and resets to idle; that hold never forwards a recording-start action. Connected holds are ignored. Disconnection, five-minute inactivity, initialization errors and interlock faults also stop/reset. The shutdown bound is one second after entering Stopping. There are no download requests or buffers in Phase 1.
- A small `.ble_reset` NOLOAD SRAM marker suppresses the PowerOn LED pattern on the BLE-return boot. It carries no recording-start flag and does not use GPREGRET. The normal boot/button disarm and read-only recovery scans are retained. The application linker and installed bootloader must preserve this marker across a software reset for the LED suppression to work; this requires a hardware check. USB-present boots retain the existing USB-aware standby policy rather than imposing a blanket USB restriction.
- Advertising LED: one 100 ms blue flash every 2 s. Connected LED: 100 ms blue flashes at 0 and 200 ms of each 5 s period. Stopping/reset/idle: LEDs off. Recording LED scheduling is unchanged outside BLE mode.

## 13.2 Phase 1 GATT contract

| Surface | UUID / content | Access |
|---|---|---|
| TacticPod service | `b9730001-7d2a-4f91-a380-8b52c029dc61` | Advertised |
| Activity | `b9730002-7d2a-4f91-a380-8b52c029dc61`; exactly one byte `0x01` | Write with response; valid writes reset inactivity |
| Battery snapshot | `b9730003-7d2a-4f91-a380-8b52c029dc61`; 6 bytes | Read |
| Device Information | Standard DIS; manufacturer/model, FICR DEVICEID as 16 hex digits, firmware revision `ble-phase1-1` | Read |

Battery bytes: version `1`; flags (bit 0 fresh, bit 1 trusted, bit 2 cell, bit 3 USB); estimated percent or `255` when unavailable; millivolts as two little-endian bytes; reserved `0`. Snapshots update while connected without changing the existing battery monitoring/calibration policy. Reading attributes, link-layer traffic and invalid writes do **not** count as valid application activity. A compatible client must send `0x01` before five minutes elapse (recommended every minute); this contract is not yet wired into TacticPod-app.

GATT is open and unauthenticated for the bench prototype. No bonding is requested. An unsolicited pairing request causes disconnect/reset; verify on hardware that the framework's security-event ordering cannot persist a bond before disconnect completes. Bluefruit's mandatory initial LittleFS mount/format remains a first-use internal-flash side effect even without pairing. The runtime checks the expected bond directories and flushes the internal-flash cache before advertising; it does not explicitly format or erase.

## 13.3 Validation plan and hardware limitations

Portable runtime tests cover lazy entry, recording/busy/fault rejection, initialization failure, advertising hold/timeout, connected inactivity/disconnection/hold-ignore, bounded reset, and a fresh button state after reset. Actual `main.cpp` runs in the ARM/Unicorn harness with a mock BLE adapter to verify ordinary recording/stop behavior, entry from 50 ms standby, USB-independent entry, all normal exit paths and the preserved session prefix.

The existing CI now builds default, reusable USB and opt-in BLE firmware, runs the full regression suite, and exports the BLE ELF/map, resource logs and selected resolved framework sources. Markdown-only changes no longer trigger unnecessary builds. Local PlatformIO still cannot resolve the Seeed package host; CI is authoritative for the actual core, toolchain and resources.

Hardware-only gates remain: first-use internal-flash backup/content review and approved provisioning; `Bluefruit.begin()` RAM negotiation and initialization latency; standby/advertising/connection current; GPIO polling/jitter and deliberate tap reliability; asynchronous disconnect, cache/flash completion and reset paths; SRAM marker/bootloader behavior; absence of accidental recording after reset; recording timing before/after BLE use. The one-second stopping bound does not interrupt a stuck synchronous `Bluefruit.begin()` or framework filesystem operation: initialization/flash hangs require bench fault-injection validation and a separately reviewed watchdog policy if encountered. No real radio, power, timing or physical reset result is implied by the simulations.
