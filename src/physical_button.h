#pragma once
#include <stdint.h>

namespace PhysicalButton {
enum class Action : uint8_t { None, Start, Stop };

class StateMachine {
 public:
  Action update(bool pressed, uint32_t now, bool recording, bool operationActive) {
    if (pressed != rawPressed) {
      rawPressed = pressed;
      rawChangedAt = now;
    }

    if (rawPressed != stablePressed && uint32_t(now - rawChangedAt) >= kDebounceMs) {
      stablePressed = rawPressed;
      if (stablePressed) {
        pressedAt = now;
        pressWasRecording = recording;
        pressWasBlocked = operationActive || !armed;
        if (recording && stopSequence == StopSequence::WaitingSecond) {
          stopSequence = uint32_t(now - firstReleaseAt) <= kSecondPressTimeoutMs
            ? StopSequence::HoldingSecond : StopSequence::Idle;
        } else if (!recording) {
          startSent = false;
        }
      } else {
        const uint32_t held = uint32_t(now - pressedAt);
        if (stopSequence == StopSequence::HoldingSecond) {
          stopSequence = StopSequence::Idle;
        } else if (stopSequence == StopSequence::StopIssued) {
          stopSequence = StopSequence::Idle;
        } else if (stopSequence == StopSequence::Idle && pressWasRecording && held <= kTapMaxMs) {
          stopSequence = StopSequence::WaitingSecond;
          firstReleaseAt = now;
        }
        armed = true;
        if (!pressWasRecording) startSent = false;
      }
    }
    if (!rawPressed && !stablePressed && uint32_t(now - rawChangedAt) >= kDebounceMs) {
      armed = true;
    }

    if (stopSequence == StopSequence::WaitingSecond &&
        uint32_t(now - firstReleaseAt) > kSecondPressTimeoutMs) {
      stopSequence = StopSequence::Idle;
    }
    if (!recording && stopSequence != StopSequence::StopIssued) {
      stopSequence = StopSequence::Idle;
    }

    if (stablePressed && !pressWasRecording && !pressWasBlocked &&
        !operationActive && !startSent && uint32_t(now - pressedAt) >= kHoldMs) {
      startSent = true;
      return Action::Start;
    }
    if (stablePressed && pressWasRecording &&
        stopSequence == StopSequence::HoldingSecond && recording &&
        uint32_t(now - pressedAt) >= kHoldMs) {
      stopSequence = StopSequence::StopIssued;
      return Action::Stop;
    }
    return Action::None;
  }

  // True while a press, release-debounce, tap or stop confirmation is in progress, or the
  // button has not yet been seen released since boot. Standby is never entered while busy.
  bool busy() const {
    return rawPressed || stablePressed || !armed || stopSequence != StopSequence::Idle;
  }

 private:
  enum class StopSequence : uint8_t { Idle, WaitingSecond, HoldingSecond, StopIssued };
  static const uint32_t kDebounceMs = 25;
  static const uint32_t kHoldMs = 3000;
  static const uint32_t kTapMaxMs = 500;
  static const uint32_t kSecondPressTimeoutMs = 5000;

  bool rawPressed = false;
  bool stablePressed = false;
  bool pressWasRecording = false;
  bool pressWasBlocked = false;
  bool startSent = false;
  bool armed = false;
  StopSequence stopSequence = StopSequence::Idle;
  uint32_t rawChangedAt = 0;
  uint32_t pressedAt = 0;
  uint32_t firstReleaseAt = 0;
};
}
