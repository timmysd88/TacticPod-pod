#pragma once
#include <stdint.h>

// Host-only groundwork: idle double-tap classifier for Bluetooth Mode. Not included by main.cpp,
// so it cannot affect production firmware. It runs beside PhysicalButton and never replaces it;
// the existing 3 s hold (Start) and the recording tap + hold (Stop) are untouched.
//
// Sampling model (caller): standby polls D0 every 50 ms; once wantsFastPoll() is true the caller
// polls every 10 ms. A press is accepted when a second LOW sample follows the first by
// >= kSecondSampleMs with no HIGH in between, so presses of roughly 60 ms or more are always
// seen by 50 ms polling, and deliberate ~100 ms taps are reliable. Shorter taps may be missed.
// The 40 ms debounce applies to release/chatter: the line must stay HIGH for kDebounceMs.
namespace BleGesture {
enum class Event : uint8_t { None, DoubleTap };

constexpr uint32_t kDebounceMs = 40;
constexpr uint32_t kSecondSampleMs = 8;  // 10 ms poll minus RTOS tick rounding.
constexpr uint32_t kTapMaxMs = 500;
constexpr uint32_t kWindowMs = 500;      // First release to the start of the second press.

class Classifier {
 public:
  // enabled must be false while recording, session-busy, faulted or BLE already active.
  Event update(bool pressed, uint32_t now, bool enabled) {
    if (!enabled) {
      reset();
      return Event::None;
    }
    if (state == State::Disarmed) {
      // After boot or a disabled period, require a stable release before accepting gestures.
      if (pressed) {
        highSince = 0;
      } else {
        if (!highSince) highSince = now ? now : 1;
        if (uint32_t(now - highSince) >= kDebounceMs) {
          state = State::Idle;
          phase = Phase::Up;
        }
      }
      return Event::None;
    }

    bool confirmed = false, released = false;
    uint32_t duration = 0;
    step(pressed, now, confirmed, released, duration);

    switch (state) {
      case State::Idle:
        if (confirmed) state = State::FirstDown;
        break;
      case State::FirstDown:
        if (phase == Phase::Down && !highSince && uint32_t(now - pressStart) > kTapMaxMs) state = State::Ignore;
        else if (released) {
          if (duration <= kTapMaxMs) {
            state = State::Gap;
            gapStart = releaseAt;
          } else {
            state = State::Idle;
          }
        }
        break;
      case State::Gap:
        if (confirmed) {
          state = uint32_t(pressStart - gapStart) <= kWindowMs ? State::SecondDown : State::FirstDown;
        } else if (phase == Phase::Up && uint32_t(now - gapStart) > kWindowMs) {
          state = State::Idle;
        }
        break;
      case State::SecondDown:
        if (phase == Phase::Down && !highSince && uint32_t(now - pressStart) > kTapMaxMs) {
          state = State::Ignore;
        } else if (released) {
          state = State::Idle;
          if (duration <= kTapMaxMs) return Event::DoubleTap;
        }
        break;
      case State::Ignore:
        if (phase == Phase::Up) state = State::Idle;
        break;
      case State::Disarmed:
        break;
    }
    return Event::None;
  }

  // True while a potential press or gesture is in progress; the caller then polls at 10 ms.
  bool wantsFastPoll() const {
    return state == State::FirstDown || state == State::Gap || state == State::SecondDown ||
           state == State::Ignore || phase != Phase::Up;
  }
  bool idle() const { return state == State::Idle && phase == Phase::Up; }

 private:
  enum class State : uint8_t { Disarmed, Idle, FirstDown, Gap, SecondDown, Ignore };
  enum class Phase : uint8_t { Up, Candidate, Down };

  void reset() {
    state = State::Disarmed;
    phase = Phase::Up;
    highSince = 0;
  }

  // Raw press tracker: candidate -> confirmed (second LOW sample) -> released (HIGH for 40 ms).
  void step(bool pressed, uint32_t now, bool& confirmed, bool& released, uint32_t& duration) {
    switch (phase) {
      case Phase::Up:
        if (pressed) {
          phase = Phase::Candidate;
          lowAt = now;
        }
        break;
      case Phase::Candidate:
        if (!pressed) {
          phase = Phase::Up;  // Single-sample glitch: not a press.
        } else if (uint32_t(now - lowAt) >= kSecondSampleMs) {
          phase = Phase::Down;
          pressStart = lowAt;
          highSince = 0;
          confirmed = true;
        }
        break;
      case Phase::Down:
        if (pressed) {
          highSince = 0;  // Chatter shorter than the debounce keeps the press alive.
        } else {
          if (!highSince) highSince = now ? now : 1;
          if (uint32_t(now - highSince) >= kDebounceMs) {
            phase = Phase::Up;
            releaseAt = highSince;
            duration = uint32_t(releaseAt - pressStart);
            released = true;
            highSince = 0;
          }
        }
        break;
    }
  }

  State state = State::Disarmed;
  Phase phase = Phase::Up;
  uint32_t lowAt = 0;
  uint32_t pressStart = 0;
  uint32_t highSince = 0;
  uint32_t releaseAt = 0;
  uint32_t gapStart = 0;
};
}
