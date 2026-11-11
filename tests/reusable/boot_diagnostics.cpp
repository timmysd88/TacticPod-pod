#include "../../src/boot_diagnostics.h"
using namespace BootDiagnostics;

int main() {
  struct Guarded { uint32_t before; Record record; uint32_t after; };
  Guarded g = {0x11223344, {{0, 0}, {0, 0}}, 0x55667788};
  if (read(g.record.current) || read(g.record.previous)) return 1;
  start(g.record);
  if (read(g.record.current) != 1 || read(g.record.previous)) return 2;
  for (uint32_t stage = 1; stage <= uint32_t(Stage::Ready); ++stage) {
    write(g.record.current, stage);
    if (read(g.record.current) != stage) return 3;
    start(g.record);
    if (read(g.record.previous) != stage || read(g.record.current) != 1) return 4;
  }
  write(g.record.current, uint32_t(Stage::Imu));
  g.record.current.inverse ^= 1;
  if (read(g.record.current)) return 5;
  start(g.record);
  if (read(g.record.previous) || read(g.record.current) != 1) return 6;
  write(g.record.current, uint32_t(Stage::Ready) + 1);
  if (read(g.record.current)) return 7;
  write(g.record.current, uint32_t(Stage::Gps));
  g.record.current.inverse = 0;
  if (read(g.record.current)) return 8;
  if (g.before != 0x11223344 || g.after != 0x55667788) return 9;
  return 0;
}
