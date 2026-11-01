#pragma once
#include <stdint.h>

// Main-loop API. TIMER4 is exclusively reserved by this module, 1MHz/32 bits.
namespace MonotonicClock {
void begin(); // Idempotent; never resets an already running boot clock.
uint64_t now();
void suspend(); // Standby only: stop TIMER4; now() continues from the RTC tick (1 ms).
void resume();  // Restart TIMER4 from the current now(); call before recording.
bool suspended();
}
