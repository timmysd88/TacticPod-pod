#if TACTICPOD_BOOT_DIAGNOSTICS
#include <Arduino.h>
#include "boot_diagnostics.h"

namespace {
volatile BootDiagnostics::Record record __attribute__((section(".boot_diagnostics"), used));
bool reportPending = true;

void preinit() {
  BootDiagnostics::start(record);
  __DSB();
}
using InitFunction = void (*)();
InitFunction const preinitEntry __attribute__((section(".preinit_array"), used)) = preinit;
}

namespace BootDiagnostics {
void checkpoint(Stage stage) {
  write(record.current, uint32_t(stage));
  __DSB();
}

void requestReport() { reportPending = true; }

void serviceReport() {
  if (!reportPending || !Serial) return;
  char line[32];
  unsigned length = 0;
  static_assert(uint32_t(Stage::Ready) <= 99, "BOOT report supports two-digit stages");
  auto append = [&](const char* text) { while (*text) line[length++] = *text++; };
  auto number = [&](uint32_t value) {
    if (value >= 10) line[length++] = char('0' + value / 10);
    line[length++] = char('0' + value % 10);
  };
  append("BOOT prev=");
  number(read(record.previous));
  append(" current=");
  number(read(record.current));
  line[length++] = '\n';
  if (Serial.availableForWrite() < int(length)) return;
  if (Serial.write(reinterpret_cast<const uint8_t*>(line), length) == length)
    reportPending = false;
}
}
#endif
