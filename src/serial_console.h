#pragma once

namespace SerialConsole {
void printMenu();
bool handleCommand(const char* line);
bool imuPrintingEnabled();
}
