#pragma once
#include <stdint.h>
#include "ble_mode.h"

#if TACTICPOD_BLE
namespace BleRuntime {
bool active();
void update(uint32_t now, const BleMode::Inputs& in);
StatusLed::Color ledColor(uint32_t now);
bool returnedFromBle();
}
#endif
