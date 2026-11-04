#pragma once
#include <stdint.h>

// Independent blue/green/red LED patterns. The XIAO nRF52840 Sense has a physical RGB
// LED (red P0.26, blue P0.06, green P0.30; active LOW); this scheduler only picks
// ONE colour at a time, so Orange (red+green) is no longer produced.
//
//   Idle / standby / low power   all OFF, no heartbeat
//   Power on (startup succeeded) 3 x BLUE, 150 ms on / 150 ms off, once
//   Recording                    solid GREEN while recording is active
//   Start refused (existing)     3 x RED, 100 ms on / 100 ms off
//   Recorder fault (existing)    3 x RED repeating every 2 s
//
// There is deliberately no power-off pattern: the firmware has no controlled power-off,
// and a physical disconnect cannot be signalled. Battery state is never shown on the LED.
namespace StatusLed {
enum class Color : uint8_t { Off, Blue, Green, Red };
enum class Event : uint8_t { None, PowerOn, StartFailed };

constexpr uint32_t kPowerOnOnMs = 150, kPowerOnOffMs = 150, kPowerOnFlashes = 3;
constexpr uint32_t kPowerOnTotalMs = kPowerOnFlashes * (kPowerOnOnMs + kPowerOnOffMs) - kPowerOnOffMs;

class Scheduler {
 public:
  void notify(Event event, uint32_t now) {
    if (event == Event::None) return;
    activeEvent = event;
    eventStartedAt = now;
  }

  // True while a one-shot pattern is still being shown (keeps the device out of standby).
  bool eventActive() const { return activeEvent != Event::None; }

  // Priority: fault > one-shot events > recording > off. Never blocks.
  Color update(uint32_t now, bool fault, bool recording) {
    if (!initialized) {
      initialized = true;
      epoch = now;
    }
    if (fault) return repeatingRed(now);

    const uint32_t eventAge = uint32_t(now - eventStartedAt);
    if (activeEvent == Event::PowerOn) {
      if (eventAge >= kPowerOnTotalMs) activeEvent = Event::None;
      else return (eventAge % (kPowerOnOnMs + kPowerOnOffMs)) < kPowerOnOnMs ? Color::Blue : Color::Off;
    } else if (activeEvent == Event::StartFailed) {
      if (eventAge >= 500) activeEvent = Event::None;
      else return burstRed(eventAge);
    }
    return recording ? Color::Green : Color::Off;
  }

 private:
  static Color burstRed(uint32_t age) {
    return (age < 100 || (age >= 200 && age < 300) || (age >= 400 && age < 500)) ? Color::Red : Color::Off;
  }

  Color repeatingRed(uint32_t now) const {
    const uint32_t phase = uint32_t(now - epoch) % 2000;
    return (phase < 100 || (phase >= 200 && phase < 300) ||
            (phase >= 400 && phase < 500)) ? Color::Red : Color::Off;
  }

  bool initialized = false;
  Event activeEvent = Event::None;
  uint32_t epoch = 0;
  uint32_t eventStartedAt = 0;
};
}
