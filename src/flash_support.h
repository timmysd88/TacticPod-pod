#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <stdint.h>
#include <string.h>
#include "flash_identity.h"

namespace FlashSupport {
enum class Manufacturer : uint8_t { Unknown, Xtx, Winbond };

inline Manufacturer manufacturer(uint8_t a, uint8_t b, uint8_t c) {
  if (b != 0x40 || c != 0x18) return Manufacturer::Unknown;
  return a == 0x0b ? Manufacturer::Xtx : a == 0xef ? Manufacturer::Winbond : Manufacturer::Unknown;
}

inline const char* name(Manufacturer m) {
  return m == Manufacturer::Xtx ? "XTX" : m == Manufacturer::Winbond ? "Winbond" : "unknown";
}

inline uint32_t le32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// JESD216 BFPT 1.0 does not encode page size; the qualified 128-Mbit
// Winbond family specifies 256-byte pages in its datasheet.
inline bool compatibleSfdp(const uint8_t* s) {
  if (memcmp(s, "SFDP", 4) || s[5] != 1 || s[7] != 0xff || s[6] >= 16) return false;
  const unsigned end = 8 + (unsigned(s[6]) + 1) * 8;
  for (unsigned i = 8; i < end; i += 8) {
    const uint8_t* p = s + i;
    if (p[0] != 0 || p[7] != 0xff || p[2] != 1 || p[3] < 9) continue;
    const uint32_t at = uint32_t(p[4]) | uint32_t(p[5]) << 8 | uint32_t(p[6]) << 16;
    if ((at & 3) || at < end || at > 256 - 36) continue;
    const uint32_t first = le32(s + at);
    uint64_t bytes = 0;
    if (((first >> 17) & 3) != 0 ||
        !FlashIdentity::capacityBytes(s, bytes) || bytes != 0x1000000ULL) return false;
    bool erase4k = false;
    for (unsigned j = 0; j < 4; ++j) {
      const uint16_t erase = uint16_t(s[at + 28 + j * 2]) | uint16_t(s[at + 29 + j * 2]) << 8;
      if (erase == 0x200c) erase4k = true;
    }
    if (p[3] >= 11) {
      if (at > 256 - 44 || ((le32(s + at + 40) >> 4) & 15) != 8) return false;
    }
    return erase4k;
  }
  return false;
}

inline Manufacturer& active() {
  static Manufacturer value = Manufacturer::Unknown;
  return value;
}
inline const char*& error() {
  static const char* value = nullptr;
  return value;
}
inline bool fail(const char* reason) {
  active() = Manufacturer::Unknown;
  error() = reason;
  return false;
}

inline void select() {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(2, LOW);
}
inline void deselect() { digitalWrite(2, HIGH); SPI.endTransaction(); }
inline uint8_t status(uint8_t opcode) {
  select(); SPI.transfer(opcode); const uint8_t result = SPI.transfer(0); deselect(); return result;
}

inline bool qualify(Manufacturer m) {
  active() = Manufacturer::Unknown;
  if (m == Manufacturer::Unknown) return fail("unsupported flash JEDEC identity");
  if (m == Manufacturer::Winbond) {
    const uint8_t sr1 = status(0x05);
    if (sr1 == 0xff || (sr1 & 3)) return fail("flash status unavailable, busy or write-enabled");
    uint8_t sfdp[256], repeat[16];
    for (unsigned pass = 0; pass < 2; ++pass) {
      for (unsigned at = 0; at < sizeof(sfdp); at += sizeof(repeat)) {
        select(); SPI.transfer(0x5a); SPI.transfer(0); SPI.transfer(0); SPI.transfer(at); SPI.transfer(0);
        for (unsigned j = 0; j < sizeof(repeat); ++j) repeat[j] = SPI.transfer(0);
        deselect();
        if (!pass) memcpy(sfdp + at, repeat, sizeof(repeat));
        else if (memcmp(sfdp + at, repeat, sizeof(repeat))) return fail("flash SFDP reads inconsistent");
      }
    }
    if (!compatibleSfdp(sfdp)) return fail("flash SFDP geometry incompatible");
  }
  active() = m;
  error() = nullptr;
  return true;
}

inline bool identify(uint32_t* observed = nullptr) {
  Manufacturer first = Manufacturer::Unknown;
  for (unsigned i = 0; i < 5; ++i) {
    select(); SPI.transfer(0x9f);
    const uint8_t a = SPI.transfer(0), b = SPI.transfer(0), c = SPI.transfer(0);
    deselect();
    if (observed) *observed = uint32_t(a) << 16 | uint32_t(b) << 8 | c;
    const Manufacturer current = manufacturer(a, b, c);
    if (current == Manufacturer::Unknown) return fail("unsupported flash JEDEC identity");
    if (i && current != first) return fail("flash JEDEC reads inconsistent");
    first = current;
  }
  if (active() == first) { error() = nullptr; return true; }
  return qualify(first);
}

inline bool writeAllowed(Manufacturer m, uint8_t sr1, uint8_t sr2, uint8_t sr3) {
  if (m == Manufacturer::Unknown) return false;
  const uint8_t sr1Mask = m == Manufacturer::Winbond ? 0x1f : 0x7f;
  return sr1 != 0xff && sr2 != 0xff && !(sr1 & sr1Mask) && !(sr2 & 0xc0) &&
    (m != Manufacturer::Winbond || (sr3 != 0xff && !(sr3 & 0x04)));
}

inline bool writable() {
  const Manufacturer m = active();
  if (m == Manufacturer::Unknown) return false;
  const uint8_t sr1 = status(0x05), sr2 = status(0x35);
  const uint8_t sr3 = m == Manufacturer::Winbond ? status(0x15) : 0;
  return writeAllowed(m, sr1, sr2, sr3);
}
}
