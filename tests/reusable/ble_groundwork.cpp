// Host-only tests for the BLE groundwork (double-tap classifier and Bluetooth Mode controller).
// Pure logic: no hardware timing is proven here; see docs/ble-prerequisites.md for bench tests.
#include <stdint.h>
#ifndef __arm__
#include <stdio.h>
#endif
#include "../../src/physical_button.h"
#include "../../src/ble_gesture.h"
#include "../../src/ble_mode.h"

// Freestanding (no heap/stdio on the ARM emulator fallback); host builds also print the failing line.
static int failures = 0;
static int firstFailure = 0;
#ifndef __arm__
#define REPORT(c) printf("FAIL line %d: %s\n", __LINE__, #c)
#else
#define REPORT(c) ((void)0)
#endif
#define CHECK(c) do { if (!(c)) { REPORT(c); ++failures; if (!firstFailure) firstFailure = __LINE__; } } while (0)

struct Press { uint32_t start, length; };
struct Vec {
  Press p[32];
  unsigned n;
  Vec() : n(0) {}
  Vec(unsigned count, Press v) : n(0) { for (unsigned i = 0; i < count; ++i) push_back(v); }
  void push_back(Press v) { if (n < 32) p[n++] = v; }
  unsigned size() const { return n; }
  const Press& operator[](unsigned i) const { return p[i]; }
};

static bool isLow(const Vec& w, uint32_t t) {
  for (unsigned i = 0; i < w.size(); ++i) if (t >= w[i].start && t < w[i].start + w[i].length) return true;
  return false;
}

struct Result { int doubleTaps; int starts; };

// Samples like the firmware: 50 ms standby poll (with a phase offset) until the classifier asks
// for fast polling, then 10 ms. fixedStep > 0 forces a constant sample period (boundary tests).
static Result run(const Vec& w, uint32_t endMs, uint32_t phase = 0, uint32_t fixedStep = 0,
                  bool withButton = false) {
  BleGesture::Classifier gesture;
  PhysicalButton::StateMachine button;
  Result r = {0, 0};
  uint32_t t = 0;
  const uint32_t first = phase;
  // Idle (released) samples before any press let the classifier arm.
  for (uint32_t s = 0; s < 200; s += 10) {
    gesture.update(false, s, true);
    button.update(false, s, false, false);
  }
  t = 200 + first;
  while (t < endMs) {
    const bool low = isLow(w, t);
    if (gesture.update(low, t, true) == BleGesture::Event::DoubleTap) ++r.doubleTaps;
    if (withButton && button.update(low, t, false, false) == PhysicalButton::Action::Start) ++r.starts;
    t += fixedStep ? fixedStep : (gesture.wantsFastPoll() ? 10 : 50);
  }
  return r;
}

static Vec taps(uint32_t at, uint32_t d1, uint32_t gap, uint32_t d2) {
  Vec w;
  w.push_back(Press{at, d1});
  w.push_back(Press{at + d1 + gap, d2});
  return w;
}

static void gestureTests() {
  // Deliberate double-tap (100 ms taps, 150 ms gap) is detected for every polling phase.
  for (uint32_t phase = 0; phase < 50; ++phase) {
    Result r = run(taps(1000, 100, 150, 100), 4000, phase);
    CHECK(r.doubleTaps == 1);
  }
  // First taps >= 61 ms are always seen by 50 ms polling plus the second 10 ms sample.
  for (uint32_t d = 61; d <= 100; ++d)
    for (uint32_t phase = 0; phase < 50; ++phase) {
      Result r = run(taps(1000, d, 150, 100), 4000, phase);
      CHECK(r.doubleTaps == 1);
    }
  // Second tap is polled at 10 ms, so a 30 ms second tap is reliable for any phase.
  for (uint32_t phase = 0; phase < 50; ++phase) CHECK(run(taps(1000, 100, 100, 30), 4000, phase).doubleTaps == 1);

  // Documented limitation: a very short first tap (< 10 ms) is never accepted.
  for (uint32_t phase = 0; phase < 50; ++phase) CHECK(run(taps(1000, 6, 150, 100), 4000, phase).doubleTaps == 0);

  // Single tap, long press, nothing: no event.
  CHECK(run(Vec(1, Press{1000, 100}), 4000).doubleTaps == 0);
  CHECK(run(Vec(1, Press{1000, 700}), 4000).doubleTaps == 0);
  CHECK(run(Vec(), 4000).doubleTaps == 0);

  // Window boundary (1 ms sampling): gap = first release to second press start.
  CHECK(run(taps(1000, 100, 499, 100), 4000, 0, 1).doubleTaps == 1);
  CHECK(run(taps(1000, 100, 500, 100), 4000, 0, 1).doubleTaps == 1);
  CHECK(run(taps(1000, 100, 501, 100), 4000, 0, 1).doubleTaps == 0);
  CHECK(run(taps(1000, 100, 900, 100), 4000, 0, 1).doubleTaps == 0);
  // Tap length boundary for both taps.
  CHECK(run(taps(1000, 500, 100, 100), 4000, 0, 1).doubleTaps == 1);
  CHECK(run(taps(1000, 501, 100, 100), 4000, 0, 1).doubleTaps == 0);
  CHECK(run(taps(1000, 100, 100, 500), 4000, 0, 1).doubleTaps == 1);
  CHECK(run(taps(1000, 100, 100, 501), 4000, 0, 1).doubleTaps == 0);
  // Gaps shorter than the 40 ms debounce are chatter, not two taps.
  CHECK(run(taps(1000, 100, 30, 100), 4000, 0, 1).doubleTaps == 0);
  CHECK(run(taps(1000, 100, 40, 100), 4000, 0, 1).doubleTaps == 0);
  CHECK(run(taps(1000, 100, 41, 100), 4000, 0, 1).doubleTaps == 1);

  // Contact bounce at press and release edges (1 ms sampling) does not break or duplicate taps.
  {
    Vec w;
    w.push_back(Press{1000, 3}); w.push_back(Press{1006, 3}); w.push_back(Press{1012, 100});            // press bounce
    w.push_back(Press{1112, 4}); w.push_back(Press{1118, 3});                                      // release bounce
    w.push_back(Press{1400, 3}); w.push_back(Press{1407, 100}); w.push_back(Press{1507, 4}); w.push_back(Press{1513, 3});
    CHECK(run(w, 4000, 0, 1).doubleTaps == 1);
  }
  // A 20 ms glitch inside a press keeps it one press; a single tap with a glitch is still one tap.
  {
    Vec w;
    w.push_back(Press{1000, 40}); w.push_back(Press{1060, 60});     // one press with a 20 ms gap
    CHECK(run(w, 4000, 0, 1).doubleTaps == 0);
    w.push_back(Press{1400, 100});                             // plus a second press: double-tap
    CHECK(run(w, 4000, 0, 1).doubleTaps == 1);
  }
  // Single-sample electrical glitches while idle never start a gesture.
  {
    Vec w;
    for (uint32_t t = 1000; t < 3000; t += 137) w.push_back(Press{t, 3});
    for (uint32_t phase = 0; phase < 10; ++phase) CHECK(run(w, 4000, phase, 10).doubleTaps == 0);
  }
  // Triple tap fires once (on the second tap); four taps fire twice.
  {
    Vec w;
    for (int i = 0; i < 3; ++i) w.push_back(Press{1000u + 250u * i, 100});
    CHECK(run(w, 4000, 0, 10).doubleTaps == 1);
    w.push_back(Press{1750, 100});
    CHECK(run(w, 4000, 0, 10).doubleTaps == 2);
  }
  // Tap then a long (>500 ms) second press: not a double-tap.
  CHECK(run(taps(1000, 100, 150, 800), 5000, 0, 10).doubleTaps == 0);
  CHECK(run(taps(1000, 100, 150, 3500), 8000, 0, 10).doubleTaps == 0);

  // Existing 3 s hold still starts exactly once and never produces a double-tap.
  {
    Result r = run(Vec(1, Press{1000, 3500}), 6000, 0, 10, true);
    CHECK(r.starts == 1 && r.doubleTaps == 0);
    // Tap followed by a 3.2 s hold: existing Start behaviour retained, no BLE.
    r = run(taps(1000, 100, 200, 3200), 8000, 0, 10, true);
    CHECK(r.starts == 1 && r.doubleTaps == 0);
    // A double-tap alone never starts recording.
    r = run(taps(1000, 100, 150, 100), 4000, 0, 10, true);
    CHECK(r.starts == 0 && r.doubleTaps == 1);
  }

  // Held at boot, then released: no event; disabled periods reset and require re-arming.
  {
    BleGesture::Classifier c;
    int events = 0;
    for (uint32_t t = 0; t < 400; t += 10) events += c.update(true, t, true) == BleGesture::Event::DoubleTap;
    CHECK(events == 0);
    // Gesture while disabled (recording / BLE active) never fires.
    for (uint32_t t = 400; t < 3000; t += 10) {
      const bool low = isLow(taps(1000, 100, 150, 100), t);
      events += c.update(low, t, false) == BleGesture::Event::DoubleTap;
    }
    CHECK(events == 0);
    // Re-enabled while the button is still held: needs a release before it arms.
    for (uint32_t t = 3000; t < 3200; t += 10) events += c.update(true, t, true) == BleGesture::Event::DoubleTap;
    CHECK(events == 0);
    CHECK(c.wantsFastPoll() == false);
  }
  // Fast poll requested right after a first sample, released after the window expires.
  {
    BleGesture::Classifier c;
    for (uint32_t t = 0; t < 100; t += 10) c.update(false, t, true);
    c.update(true, 200, true);
    CHECK(c.wantsFastPoll());
    for (uint32_t t = 210; t < 300; t += 10) c.update(true, t, true);
    for (uint32_t t = 300; t < 400; t += 10) c.update(false, t, true);
    CHECK(c.wantsFastPoll());
    for (uint32_t t = 400; t < 1200; t += 10) c.update(false, t, true);
    CHECK(c.idle() && !c.wantsFastPoll());
  }
}

static BleMode::Inputs none() { return BleMode::Inputs(); }

static void modeTests() {
  using namespace BleMode;
  // Entry guards.
  {
    Controller c; Inputs in = none(); in.doubleTap = true;
    in.recording = true; CHECK(!c.update(0, in).startAdvertising && c.current() == State::Off);
    in.recording = false; in.sessionBusy = true; CHECK(!c.update(1, in).startAdvertising && !c.active());
    in.sessionBusy = false; in.faulted = true; CHECK(!c.update(2, in).startAdvertising && !c.active());
    in.faulted = false;
    Output o = c.update(3, in);
    CHECK(o.startAdvertising && c.current() == State::Advertising && c.blocksRecording() && c.wakeLock());
    CHECK(!c.update(4, in).startAdvertising);  // Repeated double-tap is ignored.
  }
  // Off: a start hold is not touched by BLE.
  {
    Controller c; Inputs in = none(); in.startHold = true;
    CHECK(!c.update(0, in).suppressStart && !c.active());
  }
  // Advertising timeout at exactly 180 s, then reset after quiescence.
  {
    Controller c; Inputs in = none(); in.doubleTap = true; c.update(1000, in); in.doubleTap = false;
    CHECK(c.update(1000 + kAdvertiseMs - 1, in).stopAdvertising == false && c.current() == State::Advertising);
    Output o = c.update(1000 + kAdvertiseMs, in);
    CHECK(o.stopAdvertising && o.abortTransfer && c.current() == State::Stopping && c.stopReason() == StopReason::AdvertisingTimeout);
    CHECK(!c.update(1000 + kAdvertiseMs + 10, in).resetNow);
    in.quiescent = true;
    CHECK(c.update(1000 + kAdvertiseMs + 20, in).resetNow && c.current() == State::Reset);
    CHECK(!c.update(1000 + kAdvertiseMs + 30, in).resetNow && c.active());  // Reset emitted once.
  }
  // Stop bound: reset even if quiescence never reports.
  {
    Controller c; Inputs in = none(); in.doubleTap = true; c.update(0, in); in.doubleTap = false;
    c.update(kAdvertiseMs, in);
    CHECK(!c.update(kAdvertiseMs + kStopBoundMs - 1, in).resetNow);
    CHECK(c.update(kAdvertiseMs + kStopBoundMs, in).resetNow);
  }
  // 3 s hold while advertising cancels BLE, never starts recording, then resets.
  {
    Controller c; Inputs in = none(); in.doubleTap = true; c.update(0, in); in.doubleTap = false;
    in.startHold = true;
    Output o = c.update(5000, in);
    CHECK(o.suppressStart && o.stopAdvertising && c.stopReason() == StopReason::HoldCancel);
    in.startHold = false; in.quiescent = true;
    CHECK(c.update(5100, in).resetNow);
  }
  // Connected: persists beyond 180 s; 3 s hold ignored; double-tap ignored.
  {
    Controller c; Inputs in = none(); in.doubleTap = true; c.update(0, in); in.doubleTap = false;
    in.linkConnected = true; c.update(1000, in);
    CHECK(c.current() == State::Connected);
    in.validActivity = true;
    for (uint32_t t = 10000; t < 600000; t += 10000) CHECK(c.update(t, in).disconnect == false);
    in.validActivity = false; in.startHold = true; in.doubleTap = true;
    Output o = c.update(600000, in);
    CHECK(o.suppressStart && !o.abortTransfer && !o.disconnect && c.current() == State::Connected);
  }
  // Inactivity: exactly 5 min from the last valid application activity.
  {
    Controller c; Inputs in = none(); in.doubleTap = true; c.update(0, in); in.doubleTap = false;
    in.linkConnected = true; c.update(1000, in);
    in.validActivity = true; c.update(100000, in); in.validActivity = false;
    CHECK(!c.update(100000 + kInactivityMs - 1, in).disconnect);
    Output o = c.update(100000 + kInactivityMs, in);
    CHECK(o.disconnect && c.stopReason() == StopReason::Inactivity);
  }
  // Healthy long download is never cut by the 5 min timer; stall aborts only the transfer.
  {
    Controller c; Inputs in = none(); in.doubleTap = true; c.update(0, in); in.doubleTap = false;
    in.linkConnected = true; c.update(1000, in);
    in.transferActive = true;
    uint32_t t = 1000;
    for (; t < 1000 + 20 * 60000; t += 5000) {
      in.transferProgress = true;
      Output o = c.update(t, in);
      CHECK(!o.disconnect && !o.abortTransfer && c.current() == State::Connected);
    }
    in.transferProgress = false;
    const uint32_t lastProgress = t - 5000;
    CHECK(!c.update(lastProgress + kStallMs - 1, in).abortTransfer);
    Output o = c.update(lastProgress + kStallMs, in);
    CHECK(o.abortTransfer && !o.disconnect && c.current() == State::Connected);
    CHECK(!c.update(lastProgress + kStallMs + 1000, in).abortTransfer);  // Aborted once.
    // The 5 min inactivity timer starts at the abort, not before.
    CHECK(!c.update(lastProgress + kStallMs + kInactivityMs - 1, in).disconnect);
    CHECK(c.update(lastProgress + kStallMs + kInactivityMs, in).disconnect);
  }
  // Keepalives / non-transfer requests do not reset the stall timer.
  {
    Controller c; Inputs in = none(); in.doubleTap = true; c.update(0, in); in.doubleTap = false;
    in.linkConnected = true; c.update(1000, in);
    in.transferActive = true; c.update(1000, in);
    in.validActivity = true;
    bool aborted = false;
    for (uint32_t t = 1000; t <= 1000 + kStallMs; t += 5000) aborted = c.update(t, in).abortTransfer || aborted;
    CHECK(aborted);
  }
  // Normal completion: inactivity counts from the end of the transfer.
  {
    Controller c; Inputs in = none(); in.doubleTap = true; c.update(0, in); in.doubleTap = false;
    in.linkConnected = true; c.update(1000, in);
    in.transferActive = true; in.transferProgress = true; c.update(2000, in);
    in.transferProgress = false; in.transferActive = false; c.update(500000, in);
    CHECK(!c.update(500000 + kInactivityMs - 1, in).disconnect);
    CHECK(c.update(500000 + kInactivityMs, in).disconnect);
  }
  // Link loss / normal disconnect while connected aborts the transfer and leads to reset.
  {
    Controller c; Inputs in = none(); in.doubleTap = true; c.update(0, in); in.doubleTap = false;
    in.linkConnected = true; in.transferActive = true; c.update(1000, in);
    in.linkConnected = false;
    Output o = c.update(2000, in);
    CHECK(o.abortTransfer && c.stopReason() == StopReason::Disconnected && c.current() == State::Stopping);
    in.quiescent = true;
    CHECK(c.update(2010, in).resetNow);
  }
  // Interlock and fault stop BLE.
  {
    Controller c; Inputs in = none(); in.doubleTap = true; c.update(0, in); in.doubleTap = false;
    in.recording = true;
    CHECK(c.update(10, in).stopAdvertising && c.stopReason() == StopReason::Interlock);
    Controller d; Inputs f = none(); f.doubleTap = true; d.update(0, f); f.doubleTap = false;
    f.faulted = true; d.update(10, f);
    CHECK(d.stopReason() == StopReason::Fault);
  }
  // LED patterns use only Blue/Off and the approved timing.
  {
    Controller c; Inputs in = none(); in.doubleTap = true; c.update(7000, in); in.doubleTap = false;
    using StatusLed::Color;
    CHECK(c.ledColor(7000) == Color::Blue && c.ledColor(7099) == Color::Blue);
    CHECK(c.ledColor(7100) == Color::Off && c.ledColor(8999) == Color::Off);
    CHECK(c.ledColor(9000) == Color::Blue && c.led() == Led::Advertising);
    in.linkConnected = true; c.update(20000, in);
    CHECK(c.led() == Led::Connected);
    CHECK(c.ledColor(20000) == Color::Blue && c.ledColor(20099) == Color::Blue);
    CHECK(c.ledColor(20100) == Color::Off && c.ledColor(20199) == Color::Off);
    CHECK(c.ledColor(20200) == Color::Blue && c.ledColor(20299) == Color::Blue);
    CHECK(c.ledColor(20300) == Color::Off && c.ledColor(24999) == Color::Off);
    CHECK(c.ledColor(25000) == Color::Blue);
  }
}

int main() {
  gestureTests();
  modeTests();
#ifndef __arm__
  if (failures) printf("%d failure(s)\n", failures);
  else printf("ble groundwork host tests passed\n");
#endif
  return failures ? (firstFailure % 250) + 1 : 0;
}
