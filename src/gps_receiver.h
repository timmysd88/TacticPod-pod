#pragma once

namespace GpsReceiver {
void begin();
void update();
bool handleCommand(const char* line);
// True while SCAN, CONFIG or profile changes require flash/recording idle.
bool uartScanBusy();
}
