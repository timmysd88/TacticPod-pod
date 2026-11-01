#pragma once

#include <stdint.h>

namespace BatteryCalibrationStore {
struct Settings {
  uint32_t sequence = 0;
  uint32_t gainPpm = 1000000;
  int32_t offsetMv = 0;
};

enum class LoadResult { Empty, Valid, Invalid };

LoadResult load(Settings& settings);
bool save(const Settings& settings);

#if defined(TACTICPOD_BATTERY_SIM) || defined(TACTICPOD_BATTERY_STORE_SIM)
void resetForTest();
void corruptForTest(unsigned page, unsigned word);
#endif
}
