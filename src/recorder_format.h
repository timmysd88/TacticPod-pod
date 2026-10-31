#pragma once
#include <stdint.h>
#include <stddef.h>

namespace RecordFormat {
constexpr uint32_t kEnd = 0xFFF000UL;  // Exclusive; final sector belongs to diagnostics.
constexpr uint32_t kSectorSize = 4096;
constexpr uint32_t kPageSize = 256;
constexpr uint32_t kHeader = 0x31485054UL;  // TPH1
constexpr uint32_t kData = 0x31445054UL;    // TPD1
constexpr uint32_t kFooter = 0x31455054UL;  // TPE1
constexpr uint32_t kCommit = 0xC0DEC0DEUL;
constexpr unsigned kSamplesPerPage = 14;
struct Sample { uint32_t ms; int16_t axes[6]; };
static_assert(sizeof(Sample) == 16, "RAM sample layout");
inline uint32_t get32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void put32(uint8_t* p, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) p[i] = n >> (8 * i);
}
inline uint32_t crc(const uint8_t* p) {
  uint32_t result = 0xFFFFFFFFUL;
  for (unsigned i = 0; i < 248; ++i) {
    result ^= p[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      result = (result >> 1) ^ ((result & 1) ? 0xEDB88320UL : 0);
  }
  return ~result;
}
inline bool valid(const uint8_t* p) {
  return get32(p + 252) == kCommit && get32(p + 248) == crc(p);
}
inline bool blank(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; ++i) if (p[i] != 0xFF) return false;
  return true;
}
}
