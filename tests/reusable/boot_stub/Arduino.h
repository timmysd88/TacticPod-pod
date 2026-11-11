#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct MockSerial {
  bool connected = false;
  int room = 0;
  char output[128] = {};
  size_t used = 0;
  explicit operator bool() const { return connected; }
  int availableForWrite() const { return room; }
  size_t write(const uint8_t* bytes, size_t length) {
    if (length > sizeof(output) - used - 1) return 0;
    memcpy(output + used, bytes, length);
    used += length;
    output[used] = 0;
    return length;
  }
};
extern MockSerial Serial;
inline void __DSB() {}
