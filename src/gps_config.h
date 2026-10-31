#pragma once
#include <stdint.h>
namespace GpsConfig {
bool start();
bool active();
void update();
// Single UART reader calls this before TinyGPSPlus. True means UBX byte.
bool consume(uint8_t value);
}
