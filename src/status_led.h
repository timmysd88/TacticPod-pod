#pragma once
#include <stdint.h>

namespace StatusLed {
enum class Color : uint8_t { Off, Blue, Green, Orange, Red };
enum class Event : uint8_t { None, PowerOn, RecordingStarted, RecordingStopped, StartFailed };

class Scheduler {
 public:
  void notify(Event event, uint32_t now) {
    if (event == Event::None) return;
    activeEvent = event;
    eventStartedAt = now;
  }

  // True while a one-shot confirmation/failure pattern is still being shown.
  bool eventActive() const { return activeEvent != Event::None; }

  // Priority: fault > one-shot events > low battery > ready/recording pulse.
  // Periodic patterns are phased from the first update so cadence is start-to-start.
  Color update(uint32_t now, bool lowBattery, bool fault, bool recording = false) {
    if (!initialized) {
      initialized = true;
      epoch = now;
    }
    if (fault) return repeatingRed(now);

    const uint32_t eventAge = uint32_t(now - eventStartedAt);
    if (activeEvent == Event::PowerOn) {
      if (eventAge >= 1000) activeEvent = Event::None;
      else return Color::Blue;
    } else if (activeEvent == Event::RecordingStarted) {
      if (eventAge >= 1000) activeEvent = Event::None;
      else return Color::Green;
    } else if (activeEvent == Event::StartFailed) {
      if (eventAge >= 500) activeEvent = Event::None;
      else return burstRed(eventAge);
    } else if (activeEvent == Event::RecordingStopped) {
      if (eventAge >= 500) activeEvent = Event::None;
      else return eventPulse(eventAge, Color::Green);
    }

    const uint32_t sinceEpoch = uint32_t(now - epoch);
    if (lowBattery) {
      const uint32_t phase = sinceEpoch % 5000;
      return (phase < 100 || (phase >= 200 && phase < 300)) ? Color::Orange : Color::Off;
    }
    return (sinceEpoch % 2000) < 100 ? (recording ? Color::Green : Color::Blue) : Color::Off;
  }

 private:
  static Color eventPulse(uint32_t age, Color color) {
    return (age < 100 || (age >= 200 && age < 300)) ? color : Color::Off;
  }

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
