#pragma once
#include <stddef.h>
#include <stdint.h>

namespace RamObservations {
// Wire-compatible values, not native structs with implicit padding. No flash IO.
struct Imu { uint8_t bytes[24]; };
struct Rmc { uint8_t bytes[44]; };
struct Gga { uint8_t bytes[40]; };
struct Event { uint8_t bytes[32]; };
static_assert(sizeof(Imu) == 24 && sizeof(Rmc) == 44 && sizeof(Gga) == 40 && sizeof(Event) == 32,
              "Frozen V2 observation record lengths");
struct Stats {
  uint64_t observed, accepted, dropped, firstUs, lastUs, minSpacingUs, maxSpacingUs;
  unsigned used, highWater, capacity;
};
struct ParserStats { uint64_t checksumFailures, malformedFields, overlong, partial, leapSeconds; };
void begin();
void update();
bool fastImu(); // Explicit OBS START requests 100Hz RAM acquisition, no flash.
bool capturing();
void addImu(uint64_t timestampUs, const int16_t axes[6]);
void observe(uint8_t value); // Only non-UBX bytes from the existing single reader.
void interruptFrame();
bool pop(Imu& value);
bool pop(Rmc& value);
bool pop(Gga& value);
bool pop(Event& value);
Stats stats(unsigned stream); // 1 IMU, 2 RMC, 3 GGA, 4 EVENT
ParserStats parserStats();
bool handleCommand(const char* line);
}
