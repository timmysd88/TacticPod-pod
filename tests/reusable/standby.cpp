#include "../../src/flash_power.h"
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
