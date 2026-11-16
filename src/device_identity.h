#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Runtime factory identity only. No flash writes, provisioning or USB descriptors.
namespace DeviceIdentity {
constexpr char Scheme[] = "nrf52840-ficr64-v1";
constexpr char BleFirmwareRevision[] = "ble-download-2";
constexpr unsigned HexLength = 16, DecimalLength = 20;
constexpr unsigned BleSuffixLength = 8, BleNameLength = 10 + BleSuffixLength;
constexpr unsigned LegacyAdvertisingBytes = 31, InfoCapacity = 192;
static_assert(BleNameLength + 2 <= LegacyAdvertisingBytes, "Complete name must fit scan response");
static_assert(3 + 18 <= LegacyAdvertisingBytes, "Flags and existing 128-bit service must fit advertising");
struct Id {
  uint32_t low, high;
  uint64_t value() const { return uint64_t(low) | (uint64_t(high) << 32); }
};
inline bool read(Id& id) {
#ifdef NRF_FICR
  id.low = NRF_FICR->DEVICEID[0];
  id.high = NRF_FICR->DEVICEID[1];
  return true;
#else
  (void)id;
  return false; // Never fabricate an identity on an unsupported target.
#endif
}
inline void hex(const Id& id, char (&out)[HexLength + 1]) {
  const char digits[] = "0123456789abcdef";
  uint64_t v = id.value();
  for (unsigned i = HexLength; i; --i) { out[i-1] = digits[v & 15]; v >>= 4; }
  out[HexLength] = 0;
}
inline void decimal(const Id& id, char (&out)[DecimalLength + 1]) {
  char reversed[DecimalLength]; unsigned n = 0;
  uint64_t v = id.value();
  do { reversed[n++] = char('0' + v % 10); v /= 10; } while (v);
  for (unsigned i = 0; i < n; ++i) out[i] = reversed[n-1-i];
  out[n] = 0;
}
inline void bleName(const Id& id, char (&out)[BleNameLength + 1]) {
  char full[HexLength + 1]; hex(id, full);
  memcpy(out, "TacticPod-", 10);
  memcpy(out + 10, full + HexLength - BleSuffixLength, BleSuffixLength);
  out[BleNameLength] = 0; // Display-only suffix; collisions never imply equal IDs.
}
struct Writer {
  char* out; size_t capacity, used; bool ok;
  Writer(char* p, size_t n): out(p), capacity(n), used(0), ok(p && n) { if (ok) out[0] = 0; }
  void append(const char* text) {
    const size_t n = strlen(text);
    if (!ok || n >= capacity - used) { ok = false; return; }
    memcpy(out + used, text, n); used += n; out[used] = 0;
  }
  size_t finish() { if (!ok) { if (out && capacity) out[0] = 0; return 0; } return used; }
};
// Fixed tokens only: no unescaped user strings or dynamic allocations.
// Leading newline resynchronises a receiver after a disconnect/partial reply.
inline size_t info(const Id& id, bool bleBuild, char* out, size_t capacity) {
  char full[HexLength + 1], number[DecimalLength + 1]; hex(id, full); decimal(id, number);
  Writer w(out, capacity);
  w.append("\n{\"type\":\"device_info\",\"version\":1,\"uid_hex\":\""); w.append(full);
  w.append("\",\"device_id\":\""); w.append(number);
  w.append("\",\"scheme\":\""); w.append(Scheme);
  w.append("\",\"firmware\":");
  if (bleBuild) { w.append("\""); w.append(BleFirmwareRevision); w.append("\""); }
  else w.append("null");
  w.append("}\n"); return w.finish();
}
}
