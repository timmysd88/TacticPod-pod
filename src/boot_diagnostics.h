#pragma once
#include <stdint.h>

namespace BootDiagnostics {
enum class Stage : uint32_t {
  Preinit = 1, Setup, Battery, Clock, Serial, Gps, Flash, Imu, Recorder, Tcb, Ready
};

struct Slot { uint32_t value, inverse; };
struct Record { Slot current, previous; };
constexpr uint32_t Salt = 0x42544400;

inline uint32_t read(const volatile Slot& slot) {
  const uint32_t value = slot.value;
  const uint32_t stage = value ^ Salt;
  return slot.inverse == ~value && stage >= uint32_t(Stage::Preinit) &&
         stage <= uint32_t(Stage::Ready) ? stage : 0;
}

inline void write(volatile Slot& slot, uint32_t stage) {
  slot.inverse = 0;
  slot.value = Salt ^ stage;
  slot.inverse = ~slot.value;
}

inline void start(volatile Record& record) {
  const uint32_t previous = read(record.current);
  if (previous) write(record.previous, previous);
  else { record.previous.inverse = 0; record.previous.value = 0; }
  write(record.current, uint32_t(Stage::Preinit));
}

#if TACTICPOD_BOOT_DIAGNOSTICS
void checkpoint(Stage stage);
void requestReport();
void serviceReport();
#endif
}
