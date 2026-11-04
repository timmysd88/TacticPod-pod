#include "../../src/physical_button.h"
#include "../../src/status_led.h"

using PhysicalButton::Action;
using StatusLed::Color;
using StatusLed::Event;

int main() {
  {
    PhysicalButton::StateMachine button;
    if (button.update(false, 0, false, false) != Action::None) return 1;
    if (button.update(false, 25, false, false) != Action::None) return 2;
    if (button.update(true, 35, false, false) != Action::None) return 3;
    if (button.update(true, 60, false, false) != Action::None) return 4;
    if (button.update(true, 3059, false, false) != Action::None) return 5;
    if (button.update(true, 3060, false, false) != Action::Start) return 6;
    if (button.update(true, 4000, false, false) != Action::None) return 7;
  }
  {
    PhysicalButton::StateMachine button;
    button.update(true, 0, false, false);
    if (button.update(true, 25, false, false) != Action::None) return 36;
    if (button.update(true, 3025, false, false) != Action::None) return 37;
    button.update(false, 3030, false, false);
    button.update(false, 3055, false, false);
    button.update(true, 3100, false, false);
    button.update(true, 3125, false, false);
    if (button.update(true, 6125, false, false) != Action::Start) return 38;
  }
  {
    PhysicalButton::StateMachine button;
    button.update(false, 0, false, false);
    button.update(true, 10, false, true);
    button.update(true, 35, false, true);
    if (button.update(true, 3035, false, false) != Action::None) return 7;
    button.update(false, 3040, false, false);
    button.update(false, 3065, false, false);
  }
  {
    PhysicalButton::StateMachine button;
    button.update(false, 0, true, false);
    button.update(true, 10, true, false);
    button.update(true, 35, true, false);
    if (button.update(true, 3035, true, false) != Action::None) return 8;
    button.update(false, 100, true, false);
    if (button.update(false, 125, true, false) != Action::None) return 9;
    button.update(true, 3100, true, false);
    if (button.update(true, 3125, true, false) != Action::None) return 10;
    if (button.update(true, 6124, true, false) != Action::None) return 11;
    if (button.update(true, 6125, true, false) != Action::Stop) return 12;
    if (button.update(true, 7000, true, false) != Action::None) return 13;
    button.update(false, 7010, false, true);
    button.update(false, 7035, false, true);
  }
  {
    PhysicalButton::StateMachine button;
    button.update(false, 0, true, false);
    button.update(true, 10, true, false);
    button.update(true, 35, true, false);
    if (button.update(true, 3035, true, false) != Action::None) return 14;
    button.update(false, 3100, true, false);
    if (button.update(false, 3125, true, false) != Action::None) return 15;
    button.update(true, 3200, true, false);
    if (button.update(true, 3225, true, false) != Action::None) return 16;
    button.update(false, 3400, true, false);
    if (button.update(false, 3425, true, false) != Action::None) return 17;
  }
  {
    PhysicalButton::StateMachine button;
    button.update(false, 0, true, false);
    button.update(true, 10, true, false);
    button.update(true, 35, true, false);
    button.update(false, 100, true, false);
    if (button.update(false, 125, true, false) != Action::None) return 18;
    if (button.update(false, 5126, true, false) != Action::None) return 19;
  }
  {
    PhysicalButton::StateMachine button;
    button.update(false, 0xfffffe00u, false, false);
    button.update(false, 0xfffffe19u, false, false);
    button.update(true, 0xfffffff0u, false, false);
    button.update(true, 9u, false, false);
    if (button.update(true, 3008u, false, false) != Action::None) return 20;
    if (button.update(true, 3009u, false, false) != Action::Start) return 39;
  }

  {
    // Idle: LED completely off at every millisecond; no heartbeat or periodic flash.
    StatusLed::Scheduler led;
    for (uint32_t t = 0; t <= 60000; t++) if (led.update(t, false, false) != Color::Off) return 21;
    if (led.eventActive()) return 22;
  }
  {
    // Recording: solid green for as long as it lasts; off the instant it stops.
    StatusLed::Scheduler led;
    for (uint32_t t = 0; t < 30000; t++) if (led.update(t, false, true) != Color::Green) return 25;
    if (led.update(30000, false, false) != Color::Off) return 26;
    if (led.eventActive()) return 27;
    if (led.update(30001, false, true) != Color::Green) return 28;  // Recording again.
  }
  {
    // Power on: three blue flashes, 150 ms on / 150 ms off, ending after 750 ms, once.
    StatusLed::Scheduler led;
    led.notify(Event::PowerOn, 1000);
    int flashes = 0;
    bool prevOn = false;
    uint32_t edge = 1000;
    for (uint32_t t = 1000; t < 4000; t++) {
      const Color c = led.update(t, false, false);
      const uint32_t age = t - 1000;
      const bool want = age < 750 && (age % 300) < 150;
      if ((c == Color::Blue) != want) return 50;
      if (c != Color::Blue && c != Color::Off) return 51;  // Never green/red.
      const bool on = c == Color::Blue;
      if (on != prevOn) {
        if (on) { flashes++; if (flashes > 1 && t - edge != 150) return 52; }
        else if (t - edge != 150) return 53;
        edge = t;
        prevOn = on;
      }
    }
    if (flashes != 3) return 54;
    if (led.eventActive()) return 55;
    if (led.update(4000, false, false) != Color::Off) return 56;
  }
  {
    // Power on is run once: nothing repeats it without a new notify (no startup => no notify).
    StatusLed::Scheduler led;
    for (uint32_t t = 0; t < 5000; t++) if (led.update(t, false, false) != Color::Off) return 57;
  }
  {
    // Safe transitions: idle -> power on -> idle -> recording -> idle; recording never inherits blue.
    StatusLed::Scheduler led;
    led.notify(Event::PowerOn, 0);
    if (led.update(0, false, false) != Color::Blue) return 58;
    if (led.update(750, false, false) != Color::Off) return 59;
    if (led.update(760, false, true) != Color::Green) return 60;
    if (led.update(770, false, false) != Color::Off) return 61;
    // A refusal pattern landing on top of recording is bounded and then returns to solid green.
    led.notify(Event::StartFailed, 1000);
    if (led.update(1000, false, true) != Color::Red) return 62;
    if (led.update(1100, false, true) != Color::Off) return 63;
    if (led.update(1500, false, true) != Color::Green) return 64;
    if (led.eventActive()) return 65;
  }
  {
    // Fault (existing behaviour, preserved) overrides everything: three red flashes every 2 s.
    StatusLed::Scheduler led;
    led.update(0, false, true);
    if (led.update(6000, true, true) != Color::Red) return 41;
    if (led.update(6100, true, true) != Color::Off) return 42;
    if (led.update(6200, true, true) != Color::Red) return 43;
    if (led.update(6400, true, true) != Color::Red) return 44;
    if (led.update(6500, true, true) != Color::Off) return 45;
    if (led.update(8000, true, true) != Color::Red) return 46;
  }
  {
    // Recording refused (existing): three red flashes, then back to off.
    StatusLed::Scheduler led;
    led.update(0, false, false);
    led.notify(Event::StartFailed, 1000);
    if (led.update(1000, false, false) != Color::Red) return 70;
    if (led.update(1100, false, false) != Color::Off) return 71;
    if (led.update(1200, false, false) != Color::Red) return 72;
    if (led.update(1400, false, false) != Color::Red) return 73;
    if (led.update(1500, false, false) != Color::Off) return 74;
    if (led.eventActive()) return 75;
  }
  return 0;
}
