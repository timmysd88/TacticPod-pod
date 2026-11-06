#pragma once

#include <stdint.h>

namespace ImuConfig {
constexpr uint16_t AccelRangeG = 8;
constexpr uint16_t GyroRangeDps = 1000;
constexpr uint16_t SensorRateHz = 104;
constexpr uint32_t AccelMicroGPerCount = 244;
constexpr uint32_t GyroMicroDpsPerCount = 35000;

constexpr float accelGPerCount() {
  return static_cast<float>(AccelMicroGPerCount) / 1000000.0f;
}

constexpr float gyroDpsPerCount() {
  return static_cast<float>(GyroMicroDpsPerCount) / 1000000.0f;
}

constexpr float accelG(int16_t raw) {
  return static_cast<float>(raw) * accelGPerCount();
}

constexpr float gyroDps(int16_t raw) {
  return static_cast<float>(raw) * gyroDpsPerCount();
}

constexpr bool supportedStoredScales(uint32_t accelUgPerCount,
                                     uint32_t gyroMicroDpsPerCount) {
  return (accelUgPerCount == 61 && gyroMicroDpsPerCount == 8750) ||
         (accelUgPerCount == AccelMicroGPerCount &&
          gyroMicroDpsPerCount == GyroMicroDpsPerCount);
}

constexpr bool fullScaleRegistersMatch(uint8_t ctrl1Xl, uint8_t ctrl2G) {
  return (ctrl1Xl & 0x0C) == 0x0C &&
         (ctrl2G & 0x0E) == 0x08 &&
         (ctrl1Xl & 0xF0) == 0x40 &&
         (ctrl2G & 0xF0) == 0x40;
}
}
