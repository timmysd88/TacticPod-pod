#include "../../src/flash_power.h"
#include "../../src/gps_power.h"
#include "../../src/physical_button.h"
#include "../../src/standby_policy.h"
#include "../../src/status_led.h"

using Standby::Inputs;
using Standby::Policy;
using Change = Policy::Change;

struct FakeIo {
  uint8_t sr = 0;
  int commands[8] = {};
  unsigned count = 0;
  uint8_t status() { return sr; }
  void command(uint8_t c) { commands[count++ & 7] = c; }
  void waitUs(unsigned) {}
};

int main() {
  // System ON standby entry only after a quiet settle period.
  {
    Policy p; Inputs in;
    if (p.update(in, 0) != Change::None || p.update(in, 1999) != Change::None) return 1;
    if (p.update(in, 2000) != Change::Enter || !p.inStandby()) return 2;
  }
  // Every stay-awake input blocks entry; USB/serial/recording exit standby.
  {
    bool Inputs::*blockers[] = {&Inputs::recording, &Inputs::sessionBusy, &Inputs::faulted, &Inputs::usbPresent,
      &Inputs::serialOpen, &Inputs::captureActive, &Inputs::startAttempt, &Inputs::gpsBusy,
      &Inputs::buttonBusy, &Inputs::ledBusy};
    unsigned n = 0;
    for (auto field : blockers) {
      Policy p; Inputs in; in.*field = true;
      for (uint32_t t = 0; t < 10000; t += 100) if (p.update(in, t) != Change::None) return 10 + n;
      ++n;
    }
    bool Inputs::*waking[] = {&Inputs::recording, &Inputs::usbPresent, &Inputs::serialOpen, &Inputs::faulted,
      &Inputs::sessionBusy, &Inputs::startAttempt};
    for (auto field : waking) {
      Policy p; Inputs in; p.update(in, 0); p.update(in, 2000);
      in.*field = true;
      if (p.update(in, 2100) != Change::Exit || p.inStandby()) return 30 + n++;
    }
  }
  // Re-entry waits a fresh settle period after wake.
  {
    Policy p; Inputs in; p.update(in, 0); p.update(in, 2000);
    p.wake(5000);
    if (p.update(in, 6999) != Change::None || p.update(in, 7000) != Change::Enter) return 50;
  }
  // Flash deep power-down: refused when busy, write-enabled or absent; never an erase/write opcode.
  {
    FakeIo io;
    if (!FlashPower::deepPowerDown(io) || io.commands[0] != 0xB9 || io.count != 1) return 60;
    FakeIo busy; busy.sr = 1;
    if (FlashPower::deepPowerDown(busy) || busy.count) return 61;
    FakeIo wel; wel.sr = 2;
    if (FlashPower::deepPowerDown(wel) || wel.count) return 62;
    FakeIo absent; absent.sr = 0xFF;
    if (FlashPower::deepPowerDown(absent) || absent.count) return 63;
    FlashPower::release(io);
    if (io.commands[1] != 0xAB) return 64;
  }
  // Polling at 50 ms still gives a 3 s start; a 2.9 s hold cancels and standby resumes.
  {
    PhysicalButton::StateMachine b; uint32_t t = 0; bool started = false;
    for (t = 0; t <= 2900; t += 50) if (b.update(true, t, false, false) == PhysicalButton::Action::Start) return 70;
    for (; t <= 3200; t += 10) b.update(false, t, false, false);
    if (b.busy()) return 71;
    for (t = 4000; t <= 4000 + 3150; t += 50)
      if (b.update(true, t, false, false) == PhysicalButton::Action::Start) { started = true; break; }
    if (!started || t > 4000 + 3150) return 72;
  }
  // Idle poll is 100 ms released / 10 ms pressed: the 3 s hold still starts within the documented bound.
  {
    static_assert(Standby::Policy::kIdlePollMs == 100 && Standby::Policy::kPressedPollMs == 10, "poll constants");
    for (uint32_t phase = 0; phase < 100; phase += 7) {
      PhysicalButton::StateMachine b; uint32_t t = 0;
      for (; t < 1000; t += Standby::Policy::kIdlePollMs) b.update(false, t, false, false);
      const uint32_t pressAt = t - Standby::Policy::kIdlePollMs + 1 + phase % 100;  // Real press instant.
      bool started = false; uint32_t startedAt = 0; bool pressed = false;
      uint32_t next = t;
      for (uint32_t now = t; now < pressAt + 4000 && !started; now = next) {
        pressed = now >= pressAt;
        if (b.update(pressed, now, false, false) == PhysicalButton::Action::Start) { started = true; startedAt = now; }
        next = now + (pressed ? Standby::Policy::kPressedPollMs : Standby::Policy::kIdlePollMs);
      }
      if (!started || startedAt - pressAt < 3000 || startedAt - pressAt > 3000 + 150) return 73;
      PhysicalButton::StateMachine c; uint32_t u = 0;  // A 2.9 s hold must never start.
      for (; u < 1000; u += 100) c.update(false, u, false, false);
      for (; u < 1000 + 2900; u += 10) if (c.update(true, u, false, false) == PhysicalButton::Action::Start) return 74;
    }
  }
  // GPS backup request: exact UBX-RXM-PMREQ v0 frame, backup + force flags, UART wake source, valid checksum.
  {
    static_assert(TACTICPOD_GPS_BACKUP_STANDBY == 0, "GPS backup must be disabled by default");
    uint8_t f[GpsPower::kFrameSize];
    if (GpsPower::buildBackupRequest(f, 0x08) != 24) return 100;
    const uint8_t want[24] = {0xB5,0x62,0x02,0x41,0x10,0x00, 0,0,0,0, 0,0,0,0, 0x06,0,0,0, 0x08,0,0,0, 0x61,0x6B};
    for (unsigned i = 0; i < 24; ++i) if (f[i] != want[i]) return 101;
    if (f[14] != 0x06) return 102;  // MAX-M10S software standby requires force.
    GpsPower::buildBackupRequest(f, 0x12345678);
    if (f[18] != 0x78 || f[21] != 0x12) return 103;
    uint8_t a = 0, b = 0;
    for (unsigned i = 2; i < 22; ++i) { a = uint8_t(a + f[i]); b = uint8_t(b + a); }
    if (f[22] != a || f[23] != b) return 104;
  }
  // Wake retries: 1 s apart, bounded, stop once NMEA is seen.
  {
    if (GpsPower::wakeRetryDue(999, 0, 1, false) || !GpsPower::wakeRetryDue(1000, 0, 1, false)) return 110;
    if (GpsPower::wakeRetryDue(5000, 0, GpsPower::kWakeAttempts, false)) return 111;
    if (GpsPower::wakeRetryDue(5000, 0, 1, true)) return 112;
  }
  // Held at boot never starts: the machine must see a release before arming.
  {
    PhysicalButton::StateMachine b;
    for (uint32_t t = 0; t < 10000; t += 50)
      if (b.update(true, t, false, false) == PhysicalButton::Action::Start && t < 3000) return 80;
  }
  // StartFailed is a red-only event, shown while the policy defers standby.
  {
    StatusLed::Scheduler led;
    led.notify(StatusLed::Event::StartFailed, 0);
    if (!led.eventActive()) return 90;
    bool red = false;
    for (uint32_t t = 0; t < 2000; t += 10) {
      StatusLed::Color c = led.update(t, false, false, false);
      if (c == StatusLed::Color::Red) red = true;
      else if (c != StatusLed::Color::Off) return 91;
    }
    if (!red || led.update(2500, false, false, false) != StatusLed::Color::Off) return 92;
  }
  return 0;
}
