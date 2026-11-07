#include "../../src/ble_phase1.h"
#include "../../src/physical_button.h"
#include <stdint.h>

struct Adapter {
  unsigned begins = 0, stops = 0, resets = 0;
  bool link = false, activity = false, fault = false, quiet = false, beginOk = true;
  bool begin() { ++begins; return beginOk; }
  bool connected() { return link; }
  bool takeActivity() { bool a = activity; activity = false; return a; }
  bool failed() { return fault; }
  void stop() { ++stops; }
  bool quiescent() { return quiet; }
  void reset() { ++resets; }
};
typedef BlePhase1::Runtime<Adapter> Runtime;
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

int main() {
  // Boot, ordinary button holds and recording/busy/faulted entry attempts never initialize BLE.
  for (unsigned block = 0; block < 4; ++block) {
    Runtime r; Adapter a; BleMode::Inputs in;
    r.update(0, in, a); CHECK(a.begins == 0 && !r.active());
    in.startHold = true; r.update(3000, in, a); CHECK(a.begins == 0 && !r.active());
    in.startHold = false; in.doubleTap = true;
    in.recording = block == 0; in.sessionBusy = block == 1; in.faulted = block == 2;
    if (block < 3) { r.update(4000, in, a); CHECK(!r.active() && a.begins == 0); }
  }
  // Every exit maps to stop then exactly one reset; no recording action is produced.
  for (unsigned reason = 0; reason < 7; ++reason) {
    Runtime r; Adapter a; BleMode::Inputs in; in.doubleTap = true;
    a.beginOk = reason != 6;
    r.update(100, in, a); CHECK(a.begins == 1);
    in.doubleTap = false;
    if (reason == 0) r.update(180100, in, a);
    else if (reason == 1) { in.startHold = true; r.update(3100, in, a); }
    else if (reason < 6) {
      a.link = true; r.update(200, in, a);
      CHECK(r.state() == BleMode::State::Connected);
      if (reason == 2) a.link = false;
      if (reason == 3) in.recording = true;
      if (reason == 4) a.fault = true;
      r.update(reason == 5 ? 300200 : 300, in, a);
    }
    CHECK(a.stops == 1 && r.state() == BleMode::State::Stopping);
    a.quiet = true; r.update(400000, in, a);
    CHECK(a.resets == 1 && r.state() == BleMode::State::Reset);
    r.update(500000, in, a); CHECK(a.resets == 1 && a.begins == 1);
  }
  {
    Runtime r; Adapter a; BleMode::Inputs in; in.doubleTap = true;
    r.update(0, in, a); in.doubleTap = false; a.link = true;
    in.startHold = true; r.update(1, in, a); // Link-up wins a simultaneous hold cancellation.
    CHECK(r.state() == BleMode::State::Connected && a.stops == 0);
    r.update(180001, in, a); CHECK(a.stops == 0 && a.resets == 0);
    in.startHold = false; a.activity = true;
    r.update(299999, in, a);
    r.update(599998, in, a); CHECK(a.stops == 0);
    r.update(599999, in, a); CHECK(a.stops == 1);
    // Unresponsive disconnect is bounded to one second.
    r.update(600998, in, a); CHECK(a.resets == 0);
    r.update(600999, in, a); CHECK(a.resets == 1);
  }
  // A reset creates a fresh button classifier: the cancellation hold cannot start recording.
  PhysicalButton::StateMachine button;
  for (uint32_t t = 0; t < 10000; t += 10)
    CHECK(button.update(true, t, false, false) == PhysicalButton::Action::None);
  for (uint32_t t = 10000; t < 10100; t += 10) button.update(false, t, false, false);
  bool start = false;
  for (uint32_t t = 10100; t < 13300; t += 10)
    start |= button.update(true, t, false, false) == PhysicalButton::Action::Start;
  CHECK(start);
  return 0;
}
