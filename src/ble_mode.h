#pragma once
#include <stdint.h>
#include "status_led.h"

// Bluetooth Mode lifecycle, timers and interlocks. Pure logic with no hardware access;
// the opt-in BLE runtime maps Output flags onto the stack. Bluefruit's connection LED
// is disabled there with Bluefruit.autoConnLed(false) so
// only StatusLed drives the LED.
//
//   Off --double-tap (idle, no recording/session/fault)--> Advertising (<= 180 s)
//   Advertising --timeout / 3 s hold / fault--> Stopping --> Reset
//   Advertising --link up--> Connected (no 180 s limit)
//   Connected --link down / 5 min inactivity / fault--> Stopping --> Reset
//   Connected: 3 s hold ignored; stalled transfer (60 s w/o progress) is aborted only
//   Reset: terminal; the integration performs the safe reset (idle afterwards, no auto-start)
namespace BleMode {
constexpr uint32_t kAdvertiseMs = 180000;
constexpr uint32_t kInactivityMs = 300000;
constexpr uint32_t kStallMs = 60000;
constexpr uint32_t kStopBoundMs = 1000;  // Reset anyway if quiescence is not reported in time.

enum class State : uint8_t { Off, Advertising, Connected, Stopping, Reset };
enum class StopReason : uint8_t {
  None, AdvertisingTimeout, HoldCancel, Disconnected, Inactivity, Fault, Interlock
};
enum class Led : uint8_t { Off, Advertising, Connected };

struct Inputs {
  bool recording = false;
  bool sessionBusy = false;   // Tcb3Session::exclusive()
  bool faulted = false;
  bool doubleTap = false;     // BleGesture::Event::DoubleTap this tick
  bool startHold = false;     // PhysicalButton::Action::Start this tick
  bool linkConnected = false; // current link state
  bool transferActive = false;
  bool validActivity = false; // well-formed application request this tick
  bool transferProgress = false; // valid, in-order progress (e.g. correct window ACK)
  bool quiescent = false;     // adv stopped, link down, no SPI read in flight
};

struct Output {
  bool startAdvertising = false;
  bool stopAdvertising = false;
  bool abortTransfer = false;
  bool disconnect = false;
  bool resetNow = false;
  bool suppressStart = false;  // Consume the Start action; never start recording from BLE states.
};

class Controller {
 public:
  Output update(uint32_t now, const Inputs& in) {
    Output out;
    if (in.startHold && state != State::Off) out.suppressStart = true;

    switch (state) {
      case State::Off:
        // Entry only when idle; never while recording, exclusive or faulted.
        if (in.doubleTap && !in.recording && !in.sessionBusy && !in.faulted) {
          state = State::Advertising;
          enteredAt = now;
          out.startAdvertising = true;
        }
        break;

      case State::Advertising:
        if (in.recording || in.sessionBusy || in.faulted) {
          beginStop(now, in.faulted ? StopReason::Fault : StopReason::Interlock, out);
        } else if (in.linkConnected) {
          state = State::Connected;
          enteredAt = now;
          lastActivityAt = now;
          lastProgressAt = now;
          wasTransferring = false;
          stallAborted = false;
        } else if (in.startHold) {
          beginStop(now, StopReason::HoldCancel, out);
        } else if (uint32_t(now - enteredAt) >= kAdvertiseMs) {
          beginStop(now, StopReason::AdvertisingTimeout, out);
        }
        break;

      case State::Connected:
        if (in.recording || in.sessionBusy || in.faulted) {
          beginStop(now, in.faulted ? StopReason::Fault : StopReason::Interlock, out);
        } else if (!in.linkConnected) {
          beginStop(now, StopReason::Disconnected, out);
        } else {
          connectedTick(now, in, out);
        }
        break;

      case State::Stopping:
        if (in.quiescent || uint32_t(now - stopStartedAt) >= kStopBoundMs) {
          state = State::Reset;
          out.resetNow = true;
        }
        break;

      case State::Reset:
        break;  // resetNow was already emitted once.
    }
    return out;
  }

  State current() const { return state; }
  StopReason stopReason() const { return reason; }
  // Recording start paths and standby must treat any non-Off state as BLE active.
  bool active() const { return state != State::Off; }
  bool blocksRecording() const { return active(); }
  bool wakeLock() const { return active(); }

  Led led() const {
    if (state == State::Advertising) return Led::Advertising;
    if (state == State::Connected) return Led::Connected;
    return Led::Off;
  }

  // Blue 100 ms flash every 2 s while advertising; two 100 ms flashes every 5 s when connected.
  StatusLed::Color ledColor(uint32_t now) const {
    const uint32_t age = uint32_t(now - enteredAt);
    if (state == State::Advertising) {
      return (age % 2000) < 100 ? StatusLed::Color::Blue : StatusLed::Color::Off;
    }
    if (state == State::Connected) {
      const uint32_t p = age % 5000;
      return (p < 100 || (p >= 200 && p < 300)) ? StatusLed::Color::Blue : StatusLed::Color::Off;
    }
    return StatusLed::Color::Off;
  }

 private:
  void beginStop(uint32_t now, StopReason why, Output& out) {
    state = State::Stopping;
    reason = why;
    stopStartedAt = now;
    out.abortTransfer = true;
    out.stopAdvertising = true;
    out.disconnect = true;
  }

  void connectedTick(uint32_t now, const Inputs& in, Output& out) {
    if (in.validActivity) lastActivityAt = now;

    if (in.transferActive && !stallAborted) {
      if (!wasTransferring || in.transferProgress) lastProgressAt = now;
      wasTransferring = true;
      if (in.transferProgress) lastActivityAt = now;
      if (uint32_t(now - lastProgressAt) >= kStallMs) {
        // Abort the transfer only; the inactivity timer restarts from here.
        out.abortTransfer = true;
        stallAborted = true;
        lastActivityAt = now;
      }
      return;  // A healthy transfer suspends the inactivity timer.
    }
    if (wasTransferring && !in.transferActive) {
      lastActivityAt = now;  // Transfer finished or aborted: count from the end.
      wasTransferring = false;
      stallAborted = false;
    }
    if (uint32_t(now - lastActivityAt) >= kInactivityMs) {
      beginStop(now, StopReason::Inactivity, out);
    }
  }

  State state = State::Off;
  StopReason reason = StopReason::None;
  uint32_t enteredAt = 0;
  uint32_t stopStartedAt = 0;
  uint32_t lastActivityAt = 0;
  uint32_t lastProgressAt = 0;
  bool wasTransferring = false;
  bool stallAborted = false;
};
}
