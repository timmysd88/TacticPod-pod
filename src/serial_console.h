#pragma once

namespace SerialConsole {
void printMenu();
void serviceDeviceInfo(); // Bounded idle-only reply; no input or hardware writes.
bool handleCommand(const char* line);
bool imuPrintingEnabled();
}
