#pragma once
#include <stdint.h>

// Optional MAX-M10S backup standby (UBX-RXM-PMREQ, class 0x02 id 0x41, 16-byte
// payload v0). Compile-time opt-in; default OFF until verified on hardware.
// Automatic backup during normal standby. Stays 0 until the supervised test below has passed on hardware.
#ifndef TACTICPOD_GPS_BACKUP_STANDBY
#define TACTICPOD_GPS_BACKUP_STANDBY 0
#endif
// Serial command "GPS BACKUP TEST": one supervised standby/wake cycle with USB awake. Independent of the flag above.
#ifndef TACTICPOD_GPS_BACKUP_TEST
#define TACTICPOD_GPS_BACKUP_TEST 0
#endif
#if TACTICPOD_GPS_BACKUP_TEST && !TACTICPOD_REUSABLE
#error "GPS BACKUP TEST requires the reusable firmware"
#endif
// MAX-M10S / M10 SPG 5.10: wakeupSources bit3 = UART RX (either edge).
#ifndef TACTICPOD_GPS_WAKE_MASK
#define TACTICPOD_GPS_WAKE_MASK 0x08u
#endif

namespace GpsPower {
constexpr unsigned kFrameSize = 24;
constexpr uint32_t kFlagBackup = 0x02;
constexpr uint32_t kFlagForce = 0x04;  // MAX-M10S Integration Manual R04, 3.6.3.2.
constexpr uint32_t kWakeIntervalMs = 1000;  // Re-send wake bytes until NMEA returns.
constexpr unsigned kWakeAttempts = 8;
constexpr uint32_t kTxDrainMs = 100;
constexpr unsigned kWakeByteCount = 4;       // 0xFF bytes: a start-bit falling edge on receiver RX.

// duration 0 = sleep until a wake source fires. MAX-M10S software standby requires force.
inline unsigned buildBackupRequest(uint8_t* f, uint32_t wakeMask = TACTICPOD_GPS_WAKE_MASK) {
  const uint8_t head[6] = {0xB5, 0x62, 0x02, 0x41, 0x10, 0x00};
  for (unsigned i = 0; i < 6; ++i) f[i] = head[i];
  for (unsigned i = 6; i < 22; ++i) f[i] = 0;
  const uint32_t flags = kFlagBackup | kFlagForce;
  for (unsigned i = 0; i < 4; ++i) {
    f[14 + i] = uint8_t(flags >> (8 * i));
    f[18 + i] = uint8_t(wakeMask >> (8 * i));
  }
  uint8_t a = 0, b = 0;
  for (unsigned i = 2; i < 22; ++i) { a = uint8_t(a + f[i]); b = uint8_t(b + a); }
  f[22] = a; f[23] = b;
  return kFrameSize;
}

// Wake retry schedule: true when another wake pulse is due.
inline bool wakeRetryDue(uint32_t now, uint32_t lastAttempt, unsigned attempts, bool nmeaSeen) {
  return !nmeaSeen && attempts < kWakeAttempts && uint32_t(now - lastAttempt) >= kWakeIntervalMs;
}

// Non-blocking supervised cycle: request -> UART parked (as in standby) -> UART listening
// (must be silent) -> 0xFF wake attempts -> valid NMEA at 38400 -> 10 Hz re-verified -> fix status.
// It only decides; the receiver glue performs the UART actions and supplies observations.
class BackupTest {
 public:
  static const uint32_t kTxSettleMs = 5;
  static const uint32_t kDwellMs = 3000;      // Receiver parked in standby.
  static const uint32_t kListenMs = 1500;     // UART running again, no wake sent: must stay silent.
  static const uint32_t kProfileMs = 45000;   // Existing RAM audit/restore window.
  static const uint32_t kFixMs = 10000;       // Fix status is reported, not required to pass.
  enum class Phase : uint8_t { Idle, Drain, Dwell, Listen, Wake, Profile, Fix, Done };
  enum class Result : uint8_t { None, Pass, RequestNotSent, EntryNotConfirmed, NoWake, NoValidNmea,
                                ProfileFailed, Aborted };
  enum class Action : uint8_t { None, SendRequest, ParkUart, ResumeUart, SendWake };

  struct Guards {
    bool recording = false, sessionBusy = false, flashBusy = false, faulted = false;
    bool gpsBusy = false, suspended = false, uartTxBusy = false;
    bool profileVerified = true, nmeaFresh = true;
  };
  struct Obs {
    uint32_t now = 0, bytes = 0;
    bool txReady = true, validNmea = false, profileVerified = false, profileFailed = false;
    bool fix = false, guardViolated = false;
  };

  static const char* refusal(const Guards& g) {
    if (g.recording) return "recording is active";
    if (g.sessionBusy) return "a recording session is pending, finalising or exporting";
    if (g.flashBusy) return "flash/IMU capture or export is active";
    if (g.faulted) return "recorder fault is latched";
    if (g.gpsBusy) return "another GPS operation is running";
    if (g.suspended) return "GPS UART is already suspended";
    if (g.uartTxBusy) return "GPS UART transmit is busy";
    if (!g.profileVerified) return "10 Hz profile is not verified yet (wait for startup audit)";
    if (!g.nmeaFresh) return "no fresh NMEA from the receiver";
    return nullptr;
  }

  bool running() const { return phase != Phase::Idle && phase != Phase::Done; }
  bool done() const { return phase == Phase::Done; }
  Phase current() const { return phase; }
  Result result() const { return res; }
  bool start(const Guards& g, uint32_t now) {
    if (running() || refusal(g)) return false;
    *this = BackupTest();
    phase = Phase::Drain; wantRequest = true; phaseAt = now;
    return true;
  }
  // The glue reports whether the 24-byte request was accepted by the UART.
  void requestSent(bool ok, uint32_t now) {
    sent = ok; phaseAt = now;
    if (!ok) finish(Result::RequestNotSent);
  }
  bool parked() const { return uartParked; }
  bool observingFailure() const { return phase == Phase::Listen && res == Result::EntryNotConfirmed; }
  // Called by the RX reader on the first unexpected non-UBX byte, before any
  // other services can run. Failure is sticky; only observation continues.
  void unexpectedRx(uint32_t now, uint32_t bytes) {
    if (phase == Phase::Listen && res == Result::None) {
      res = Result::EntryNotConfirmed; silent = false; failureAt = now;
      listenBytes = bytes - baseBytes;
    }
  }

  Action step(const Obs& o) {
    if (!running()) return Action::None;
    if (o.guardViolated) {
      const bool resume = uartParked;
      uartParked = false;
      finish(res == Result::EntryNotConfirmed ? res : Result::Aborted);
      return resume ? Action::ResumeUart : Action::None;
    }
    const uint32_t el = uint32_t(o.now - phaseAt);
    switch (phase) {
      case Phase::Drain:
        if (wantRequest) { wantRequest = false; return Action::SendRequest; }
        if ((o.txReady && el >= kTxSettleMs) || el >= 100 + kTxSettleMs) {
          uartParked = true; phase = Phase::Dwell; phaseAt = o.now;
          return Action::ParkUart;
        }
        break;
      case Phase::Dwell:
        if (el >= kDwellMs) {
          uartParked = false; phase = Phase::Listen; phaseAt = o.now; baseBytes = o.bytes;
          return Action::ResumeUart;
        }
        break;
      case Phase::Listen:
        if (o.bytes != baseBytes) unexpectedRx(o.now, o.bytes);
        if (observingFailure()) {
          listenBytes = o.bytes - baseBytes;
          if (uint32_t(o.now - failureAt) >= kListenMs) finish(Result::EntryNotConfirmed);
        } else if (el >= kListenMs) {
          silent = true; phase = Phase::Wake; phaseAt = o.now; lastWakeAt = o.now; wakes = 1;
          return Action::SendWake;
        }
        break;
      case Phase::Wake:
        if (o.validNmea) {
          wakeValid = true; validAfterMs = el; baudOk = true;
          phase = Phase::Profile; phaseAt = o.now;
        } else if (wakes < kWakeAttempts && uint32_t(o.now - lastWakeAt) >= kWakeIntervalMs) {
          ++wakes; lastWakeAt = o.now;
          return Action::SendWake;
        } else if (wakes >= kWakeAttempts && uint32_t(o.now - lastWakeAt) >= kWakeIntervalMs) {
          listenBytes = o.bytes - baseBytes;
          finish(listenBytes ? Result::NoValidNmea : Result::NoWake);
        }
        break;
      case Phase::Profile:
        if (o.profileFailed) { profileChecked = true; finish(Result::ProfileFailed); }
        else if (o.validNmea && o.profileVerified) { profileOk = true; profileChecked = true; phase = Phase::Fix; phaseAt = o.now; }
        else if (el >= kProfileMs) { profileChecked = true; finish(Result::ProfileFailed); }
        break;
      case Phase::Fix:
        // Fix is optional; continued NMEA/profile recovery is not.
        if (!o.validNmea || !o.profileVerified || o.profileFailed) {
          profileOk = false; finish(Result::ProfileFailed);
        } else if (o.fix) { fixOk = true; finish(Result::Pass); }
        else if (el >= kFixMs) finish(Result::Pass);
        break;
      default: break;
    }
    return Action::None;
  }

  bool requestOk() const { return sent; }
  bool standbySilent() const { return silent; }
  unsigned wakeAttempts() const { return wakes; }
  bool wakeSucceeded() const { return wakeValid; }
  uint32_t validAfter() const { return validAfterMs; }
  uint32_t strayBytes() const { return listenBytes; }
  bool baudConfirmed() const { return baudOk; }
  bool profileWasChecked() const { return profileChecked; }
  bool profileVerified() const { return profileOk; }
  bool fixSeen() const { return fixOk; }
  // A receiver that did not come back must block recording until NMEA and the 10 Hz profile return.
  bool blocksRecording() const {
    return res == Result::NoWake || res == Result::NoValidNmea || res == Result::ProfileFailed;
  }

 private:
  void finish(Result r) { res = r; phase = Phase::Done; }
  Phase phase = Phase::Idle;
  Result res = Result::None;
  uint32_t phaseAt = 0, lastWakeAt = 0, baseBytes = 0, validAfterMs = 0, listenBytes = 0, failureAt = 0;
  unsigned wakes = 0;
  bool wantRequest = false, sent = false, uartParked = false, silent = false;
  bool wakeValid = false, baudOk = false, profileChecked = false, profileOk = false, fixOk = false;
};
}
