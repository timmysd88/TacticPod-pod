#pragma once
#include "gps_power.h"

namespace GpsPower {
// Observation only. No I/O, allocation, waits or changes to test decisions.
// Timestamps are host UART read times, not wire/interrupt arrival timestamps.
struct BackupTrace {
  static constexpr unsigned kPhases = 8, kSamples = 96, kEvents = 24;
  struct Sample { uint32_t ms; uint8_t value; };
  struct Window {
    uint32_t bytes = 0, valid = 0, badChecksum = 0, first = 0, last = 0;
    unsigned stored = 0;
  } windows[kPhases];
  // Preserve separate bounded prefixes so drain traffic cannot exhaust listen/wake capture.
  Sample samples[3][kSamples];
  struct Event { uint32_t ms; uint8_t phase, action; bool txReady; } events[kEvents];
  uint32_t started = 0, pendingAtStart = 0, pendingAtPark = 0;
  unsigned eventCount = 0;
  void reset(uint32_t now) {
    started = now; pendingAtStart = pendingAtPark = 0; eventCount = 0;
    // Old samples are inaccessible once stored/event counts are zero. Avoid a
    // multi-KiB temporary on the embedded loop task's stack.
    for (auto& w : windows) w = Window();
  }
  static int slot(BackupTest::Phase p) {
    return p == BackupTest::Phase::Drain ? 0 : p == BackupTest::Phase::Listen ? 1 :
           p == BackupTest::Phase::Wake ? 2 : -1;
  }
  void event(uint32_t now, BackupTest::Phase p, BackupTest::Action a, bool ready) {
    if (eventCount < kEvents) events[eventCount++] = {uint32_t(now - started), uint8_t(p), uint8_t(a), ready};
  }
  void observe(uint32_t now, BackupTest::Phase p, uint8_t value, bool valid, bool badChecksum = false) {
    Window& w = windows[unsigned(p)];
    const uint32_t at = uint32_t(now - started);
    if (!w.bytes) w.first = at;
    ++w.bytes; w.last = at; if (valid) ++w.valid;
    if (badChecksum) ++w.badChecksum;
    const int s = slot(p);
    if (s >= 0 && w.stored < kSamples) samples[s][w.stored++] = {at, value};
  }
};
}
