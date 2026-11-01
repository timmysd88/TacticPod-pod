#pragma once
#include <stdint.h>

namespace StatusLed {
enum class Color : uint8_t { Off, Blue, Green, Orange, Red };
enum class Event : uint8_t { None, StartupAccepted, RecordingStarted, RecordingStopped };

class Scheduler {
 public:
  void notify(Event event, uint32_t now) {
    if (event == Event::None) return;
    activeEvent = event;
    eventStartedAt = now;
  }

  Color update(uint32_t now, bool gpsFix, bool lowBattery, bool fault) {
    if (!initialized) {
      initialized = true;
      epoch = now;
    }
    if (fault) return repeatingRed(now);
    if (lowBattery) return (uint32_t(now - epoch) % 3000) < 100 ? Color::Orange : Color::Off;

    const uint32_t eventAge = uint32_t(now - eventStartedAt);
    if (activeEvent == Event::StartupAccepted) {
      if (eventAge >= 500) activeEvent = Event::None;
      else return eventPulse(eventAge, Color::Blue);
    } else if (activeEvent == Event::RecordingStarted) {
      if (eventAge >= 400) activeEvent = Event::None;
      else return Color::Blue;
    } else if (activeEvent == Event::RecordingStopped) {
      if (eventAge >= 500) activeEvent = Event::None;
      else return eventPulse(eventAge, Color::Green);
    }

    const uint32_t phase = uint32_t(now - epoch) % 3000;
    if (phase >= 100) return Color::Off;
    const uint32_t pulse = uint32_t(now - epoch) / 3000;
    return gpsFix && (pulse & 1) ? Color::Green : Color::Blue;
  }

 private:
  static Color eventPulse(uint32_t age, Color color) {
    return (age < 100 || (age >= 200 && age < 300)) ? color : Color::Off;
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
