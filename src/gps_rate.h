#pragma once
#include <stdint.h>

namespace GpsRate {
bool start();
void observe(uint8_t value);
void update();
bool active();
}
