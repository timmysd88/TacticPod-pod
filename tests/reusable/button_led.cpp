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
    StatusLed::Scheduler led;
    if (led.update(0, false, false, false) != Color::Blue) return 21;
    if (led.update(100, false, false, false) != Color::Off) return 22;
    if (led.update(3000, true, false, false) != Color::Green) return 23;
    if (led.update(6000, true, true, false) != Color::Orange) return 24;
    if (led.update(6100, true, true, false) != Color::Off) return 25;
    if (led.update(6200, true, true, true) != Color::Red) return 26;
    if (led.update(6300, true, false, true) != Color::Off) return 27;

    led.notify(Event::StartupAccepted, 7000);
    if (led.update(7000, false, false, false) != Color::Blue) return 28;
    if (led.update(7100, false, false, false) != Color::Off) return 29;
    if (led.update(7200, false, false, false) != Color::Blue) return 30;
    led.notify(Event::RecordingStarted, 8000);
    if (led.update(8399, false, false, false) != Color::Blue) return 31;
    if (led.update(8400, false, false, false) != Color::Off) return 32;
    led.notify(Event::RecordingStopped, 9000);
    if (led.update(9000, false, false, false) != Color::Green) return 33;
    if (led.update(9100, false, false, false) != Color::Off) return 34;
    if (led.update(9200, false, false, false) != Color::Green) return 35;
  }

  return 0;
}
