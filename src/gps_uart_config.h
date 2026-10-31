#pragma once
#include <Arduino.h>

namespace GpsUartConfig {
// User's hardware scan confirmed valid MAX-M10S NMEA at this rate.
// This configures only the XIAO, not the GPS module.
constexpr uint32_t kNormalBaud = 38400;
constexpr uint32_t kNormalBaudRegister = UARTE_BAUDRATE_BAUDRATE_Baud38400;
constexpr size_t kRawBufferBytes = 49152;
static_assert(kRawBufferBytes >= (kNormalBaud / 10) * 10 + SERIAL_BUFFER_SIZE,
              "RAW RAM must cover ten seconds of 8N1 plus queued UART bytes");
}
