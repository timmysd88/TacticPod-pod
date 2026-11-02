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
    // Ready: 100 ms blue, start-to-start cadence exactly 2 s; GPS plays no part.
    StatusLed::Scheduler led;
    int starts = 0;
    uint32_t lastStart = 0;
    bool prevOn = false;
    for (uint32_t t = 0; t <= 10000; t++) {
      const Color c = led.update(t, false, false, false);
      const bool on = c != Color::Off;
      if (on && c != Color::Blue) return 21;
      if (on && !prevOn) {
        if (starts && t - lastStart != 2000) return 22;
        lastStart = t;
        starts++;
      }
      if ((t % 2000 < 100) != on) return 23;
      prevOn = on;
    }
    if (starts != 6) return 24;
  }
  {
    // Recording: 100 ms green every 2 s.
    StatusLed::Scheduler led;
    if (led.update(0, false, false, true) != Color::Green) return 25;
    if (led.update(99, false, false, true) != Color::Green) return 26;
    if (led.update(100, false, false, true) != Color::Off) return 27;
    if (led.update(1999, false, false, true) != Color::Off) return 28;
    if (led.update(2000, false, false, true) != Color::Green) return 29;
  }
  {
    // Low battery: two 100 ms orange flashes every 5 s, overriding ready and recording.
    for (int rec = 0; rec < 2; rec++) {
      StatusLed::Scheduler led;
      const bool r = rec;
      if (led.update(0, true, false, r) != Color::Orange) return 30;
      if (led.update(100, true, false, r) != Color::Off) return 31;
      if (led.update(200, true, false, r) != Color::Orange) return 32;
      if (led.update(300, true, false, r) != Color::Off) return 33;
      if (led.update(2000, true, false, r) != Color::Off) return 34;
      if (led.update(5000, true, false, r) != Color::Orange) return 35;
      if (led.update(5200, true, false, r) != Color::Orange) return 36;
      if (led.update(5400, true, false, r) != Color::Off) return 37;
    }
  }
  {
    // Fault repeats three red flashes and overrides everything.
    StatusLed::Scheduler led;
    led.update(0, true, false, true);
    if (led.update(6000, true, true, true) != Color::Red) return 41;
    if (led.update(6100, true, true, true) != Color::Off) return 42;
    if (led.update(6200, true, true, true) != Color::Red) return 43;
    if (led.update(6400, true, true, true) != Color::Red) return 44;
    if (led.update(6500, true, true, true) != Color::Off) return 45;
    if (led.update(8000, true, true, true) != Color::Red) return 46;
  }
  {
    // Power on: solid blue 1 s, then ready pulse (next at 2 s).
    StatusLed::Scheduler led;
    led.notify(Event::PowerOn, 0);
    if (led.update(0, false, false, false) != Color::Blue) return 50;
    if (led.update(999, false, false, false) != Color::Blue) return 51;
    if (!led.eventActive()) return 52;
    if (led.update(1000, false, false, false) != Color::Off) return 53;
    if (led.eventActive()) return 54;
    if (led.update(2000, false, false, false) != Color::Blue) return 55;
  }
  {
    // Recording started: solid green 1 s, then green pulse; stopped: two green flashes.
    StatusLed::Scheduler led;
    led.update(0, false, false, false);
    led.notify(Event::RecordingStarted, 3000);
    if (led.update(3000, false, false, true) != Color::Green) return 60;
    if (led.update(3999, false, false, true) != Color::Green) return 61;
    if (led.update(4000, false, false, true) != Color::Green) return 62;  // 2 s pulse phase
    if (led.update(4100, false, false, true) != Color::Off) return 63;
    led.notify(Event::RecordingStopped, 9001);
    if (led.update(9001, false, false, false) != Color::Green) return 64;
    if (led.update(9101, false, false, false) != Color::Off) return 65;
    if (led.update(9201, false, false, false) != Color::Green) return 66;
    if (led.update(9301, false, false, false) != Color::Off) return 67;
    if (led.update(9501, false, false, false) != Color::Off) return 68;
    if (led.eventActive()) return 69;
  }
  {
    // Recording refused: three red flashes, then back to ready.
    StatusLed::Scheduler led;
    led.update(0, false, false, false);
    led.notify(Event::StartFailed, 1000);
    if (led.update(1000, false, false, false) != Color::Red) return 70;
    if (led.update(1100, false, false, false) != Color::Off) return 71;
    if (led.update(1200, false, false, false) != Color::Red) return 72;
    if (led.update(1400, false, false, false) != Color::Red) return 73;
    if (led.update(1500, false, false, false) != Color::Off) return 74;
    if (led.eventActive()) return 75;
  }
  return 0;
}
