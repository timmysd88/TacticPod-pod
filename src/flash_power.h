#pragma once
#include <stdint.h>

// SPI NOR deep power-down helpers. Io supplies: uint8_t status() (opcode 0x05),
// void command(uint8_t) (one CS-low single-byte command), void waitUs(unsigned).
// Deep power-down is never entered while WIP/WEL are set, and the chip is always
// released at boot because a reset can occur while it sleeps. No erase/write opcode here.
namespace FlashPower {
constexpr uint8_t kDeepPowerDown = 0xB9;
constexpr uint8_t kRelease = 0xAB;
constexpr unsigned kEnterDelayUs = 5;
constexpr unsigned kReleaseDelayUs = 100;  // Conservative against typical tRES1 of 20 us.

template <class Io>
bool deepPowerDown(Io& io) {
  const uint8_t status = io.status();
  if (status == 0xFF || (status & 0x03) != 0) return false;
  io.command(kDeepPowerDown);
  io.waitUs(kEnterDelayUs);
  return true;
}

template <class Io>
void release(Io& io) {
  io.command(kRelease);
  io.waitUs(kReleaseDelayUs);
}
}
