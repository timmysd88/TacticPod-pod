#pragma once
#include "tcb3_test_limits.h"
#if TACTICPOD_REUSABLE
#include "battery_monitor.h"
#include "battery_gauge.h"
namespace BatteryRuntime {
constexpr unsigned kBurstSamples=5; // Conversions per measurement (real ADC backend).
void begin();
void update(bool allowStart=true);
Battery::Monitor& monitor();
bool command(const char* line); // RAM-only configuration; caller requires idle.
unsigned format(char*,unsigned); // Legacy single machine-readable "BAT source=..." line.
void noteLoad(bool active); // Recording/flash activity: voltage sags, so estimates may not rise.
// Snapshot, then one human-readable line per formatLine() call (0 when finished).
struct Report {
  bool fresh=false,trusted=false,calibrated=false,cell=false,usb=false,estimate=false,chargeInfluenced=false;
  uint32_t mv=0,filteredMv=0;unsigned percent=0;Battery::Category category=Battery::Category::Unknown;
};
void capture(Report&);
unsigned formatLine(const Report&,unsigned index,char*,unsigned capacity);
uint32_t timeouts();
bool usbPresent();
#ifdef TACTICPOD_BATTERY_SIM
void inject(uint32_t millivolts,bool valid=true); // Host harness only, no USB command.
#endif
}
#else
namespace BatteryRuntime {inline void begin(){} }
#endif
