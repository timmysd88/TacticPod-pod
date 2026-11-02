#pragma once
#include <stdint.h>

// Optional MAX-M10S backup standby (UBX-RXM-PMREQ, class 0x02 id 0x41, 16-byte
// payload v0). Compile-time opt-in; default OFF until verified on hardware.
#ifndef TACTICPOD_GPS_BACKUP_STANDBY
#define TACTICPOD_GPS_BACKUP_STANDBY 0
#endif
// wakeupSources: bit3 = uartrx (u-blox/SparkFun docs). ubxlib uses 0xE4; override if hardware proves otherwise.
#ifndef TACTICPOD_GPS_WAKE_MASK
#define TACTICPOD_GPS_WAKE_MASK 0x08u
#endif

namespace GpsPower {
constexpr unsigned kFrameSize = 24;
constexpr uint32_t kFlagBackup = 0x02;
constexpr uint32_t kWakeIntervalMs = 1000;  // Re-send wake bytes until NMEA returns.
constexpr unsigned kWakeAttempts = 8;
constexpr uint32_t kTxDrainMs = 100;
constexpr unsigned kWakeByteCount = 4;       // 0xFF bytes: a start-bit falling edge on receiver RX.

// duration 0 = sleep until a wake source fires. Backup mode; never sets "force".
inline unsigned buildBackupRequest(uint8_t* f, uint32_t wakeMask = TACTICPOD_GPS_WAKE_MASK) {
  const uint8_t head[6] = {0xB5, 0x62, 0x02, 0x41, 0x10, 0x00};
  for (unsigned i = 0; i < 6; ++i) f[i] = head[i];
  for (unsigned i = 6; i < 22; ++i) f[i] = 0;
  const uint32_t flags = kFlagBackup;
  for (unsigned i = 0; i < 4; ++i) {
    f[14 + i] = uint8_t(flags >> (8 * i));
    f[18 + i] = uint8_t(wakeMask >> (8 * i));
  }
  uint8_t a = 0, b = 0;
  for (unsigned i = 2; i < 22; ++i) { a = uint8_t(a + f[i]); b = uint8_t(b + a); }
  f[22] = a; f[23] = b;
  return kFrameSize;
}

// Wake retry schedule: true when another wake pulse is due.
inline bool wakeRetryDue(uint32_t now, uint32_t lastAttempt, unsigned attempts, bool nmeaSeen) {
  return !nmeaSeen && attempts < kWakeAttempts && uint32_t(now - lastAttempt) >= kWakeIntervalMs;
}
}
