#define TACTICPOD_BOOT_DIAGNOSTICS 1
#include "boot_stub/Arduino.h"
MockSerial Serial;
#include "../../src/boot_diagnostics.cpp"

int main() {
  preinit();
  BootDiagnostics::checkpoint(BootDiagnostics::Stage::Imu);
  preinit();
  BootDiagnostics::checkpoint(BootDiagnostics::Stage::Ready);
  BootDiagnostics::serviceReport();
  if (Serial.used) return 1;
  Serial.connected = true;
  Serial.room = 5;
  BootDiagnostics::serviceReport();
  if (Serial.used) return 2;
  Serial.room = 128;
  BootDiagnostics::serviceReport();
  if (strcmp(Serial.output, "BOOT prev=8 current=11\n")) return 3;
  BootDiagnostics::serviceReport();
  if (strcmp(Serial.output, "BOOT prev=8 current=11\n")) return 4;
  BootDiagnostics::requestReport();
  BootDiagnostics::serviceReport();
  if (strcmp(Serial.output, "BOOT prev=8 current=11\nBOOT prev=8 current=11\n")) return 5;
  return 0;
}
