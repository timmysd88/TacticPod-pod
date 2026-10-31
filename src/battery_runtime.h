#pragma once
#include "tcb3_test_limits.h"
#if TACTICPOD_REUSABLE
#include "battery_monitor.h"
namespace BatteryRuntime {
void begin();
void update(bool allowStart=true);
Battery::Monitor& monitor();
bool command(const char* line); // RAM-only configuration; caller requires idle.
unsigned format(char*,unsigned);
uint32_t timeouts();
bool usbPresent();
#ifdef TACTICPOD_BATTERY_SIM
void inject(uint32_t millivolts,bool valid=true); // Host harness only, no USB command.
#endif
}
#else
namespace BatteryRuntime {inline void begin(){} }
#endif
