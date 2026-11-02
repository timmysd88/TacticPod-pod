#pragma once
#include <stdint.h>

// Portable System ON standby policy. It decides only WHEN the application may
// idle; it never starts a recording and never touches battery/charger pins.
namespace Standby {
struct Inputs {
  bool recording = false;      // Recorder owns IMU/GPS/flash.
  bool sessionBusy = false;    // Pending scan, finalising, exporting or fault-locked.
  bool faulted = false;        // Recoverable fault: stay awake and keep the red LED.
  bool usbPresent = false;     // VBUS: USB diagnostics/recovery has priority.
  bool serialOpen = false;
  bool gpsBusy = false;        // Receiver start-up/profile/scan still running.
  bool captureActive = false;  // Manual OBS/COMP capture running.
  bool buttonBusy = false;     // Press, tap or stop-confirmation in progress.
  bool ledBusy = false;        // Confirmation/failure pattern still showing.
  bool startAttempt = false;   // Button start accepted but not yet recording.
};

class Policy {
 public:
  static const uint32_t kSettleMs = 2000;
  static const uint32_t kIdlePollMs = 100;    // Released button: fewer CPU wake-ups.
  static const uint32_t kPressedPollMs = 10;  // Pressed: keep the 3000 ms hold accurate.
  enum class Change : uint8_t { None, Enter, Exit };

  Change update(const Inputs& in, uint32_t now) {
    const bool mustStayAwake = in.recording || in.sessionBusy || in.faulted || in.usbPresent ||
      in.serialOpen || in.captureActive || in.startAttempt;
    if (standby) {
      if (mustStayAwake) {
        standby = false;
        busySince = now;
        return Change::Exit;
      }
      return Change::None;
    }
    if (mustStayAwake || in.gpsBusy || in.buttonBusy || in.ledBusy) {
      busySince = now;
      return Change::None;
    }
    if (uint32_t(now - busySince) >= kSettleMs) {
      standby = true;
      return Change::Enter;
    }
    return Change::None;
  }

  // The caller wakes peripherals first, then asks for a session. Never the reverse.
  void wake(uint32_t now) {
    standby = false;
    busySince = now;
  }

  // Abandon standby without a wake (e.g. peripheral power-down was refused).
  void refuse(uint32_t now) { wake(now); }

  bool inStandby() const { return standby; }

 private:
  bool standby = false;
  uint32_t busySince = 0;
};
}
