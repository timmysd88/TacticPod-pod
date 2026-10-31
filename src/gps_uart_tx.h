#pragma once
#include <Arduino.h>

// The Seeed BSP takes a semaphore with portMAX_DELAY in Uart::write(). Use
// exactly one DMA batch, and check completion before ever taking it again.
// All Serial1 writers must use this shared tracker.
namespace GpsUartTx {
inline size_t& previousSize() { static size_t size = 0; return size; }
inline bool ready() {
  return previousSize() == 0 ||
      (NRF_UARTE0->TXD.AMOUNT == previousSize() && NRF_UARTE0->EVENTS_ENDTX == 0);
}
inline size_t write(const uint8_t* data, size_t size) {
  if (!size || size > SERIAL_BUFFER_SIZE || !ready()) return 0;
  previousSize() = size;
  return Serial1.write(data, size);
}
// Only after ready() and a controlled peripheral stop (GPS SCAN).
inline void resetIdle() { previousSize() = 0; }
}
