#pragma once
#include <stdint.h>
namespace GpsConfig {
bool start();
bool active();
void update();
void resetParser(); // Only with exclusive UART ownership / a controlled baud transition.
// Single UART reader calls this before TinyGPSPlus. True means UBX byte.
bool consume(uint8_t value);
}
