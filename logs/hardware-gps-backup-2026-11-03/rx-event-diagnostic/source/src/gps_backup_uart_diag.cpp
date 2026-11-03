#include "gps_backup_uart_diag.h"
#if TACTICPOD_GPS_BACKUP_TEST
#include <Arduino.h>
#include "monotonic_clock.h"
namespace {
GpsBackupUartDiag::Snapshot snapshots[GpsBackupUartDiag::Count];
volatile bool present[GpsBackupUartDiag::Count] = {};
volatile bool active = false, firstRx = false;
volatile uint32_t errors = 0;
uint32_t origin;
void rememberErrors(uint32_t bits) {
  const uint32_t mask = __get_PRIMASK();
  __disable_irq(); errors |= bits;
  if (!mask) __enable_irq();
}
}
namespace GpsBackupUartDiag {
void start() {
  active = firstRx = false;
  for (unsigned i = 0; i < Count; ++i) present[i] = false;
  errors = 0; origin = uint32_t(MonotonicClock::now()); active = true;
}
void capture(Tag tag) {
  if (!active || present[tag]) return;
  Snapshot& s = snapshots[tag];
  s.us = uint32_t(MonotonicClock::now()) - origin;
  s.endrx = NRF_UARTE0->EVENTS_ENDRX; s.rxdrdy = NRF_UARTE0->EVENTS_RXDRDY;
  s.rxstarted = NRF_UARTE0->EVENTS_RXSTARTED; s.rxto = NRF_UARTE0->EVENTS_RXTO;
  s.error = NRF_UARTE0->EVENTS_ERROR; s.amount = NRF_UARTE0->RXD.AMOUNT;
  s.errorsrc = NRF_UARTE0->ERRORSRC; rememberErrors(s.errorsrc);
  s.inten = NRF_UARTE0->INTENSET; s.enabled = NRF_UARTE0->ENABLE;
  s.irqEnabled = NVIC_GetEnableIRQ(UARTE0_UART0_IRQn);
  s.irqPending = NVIC_GetPendingIRQ(UARTE0_UART0_IRQn);
  s.irqActive = NVIC_GetActive(UARTE0_UART0_IRQn); s.primask = __get_PRIMASK();
  s.txAmount = NRF_UARTE0->TXD.AMOUNT; s.endtx = NRF_UARTE0->EVENTS_ENDTX;
  __DMB(); present[tag] = true;
}
void armFirstRx() { firstRx = true; }
void stop() { active = firstRx = false; }
bool get(Tag tag, Snapshot& value) {
  if (!present[tag]) return false;
  __DMB(); value = snapshots[tag]; return true;
}
uint32_t errorsSeen() { return errors; }
void observeErrors() { if (active) rememberErrors(NRF_UARTE0->ERRORSRC); }
}
// Test environments only: a linker alias redirects the vector and always calls
// the original BSP ISR. No vendor driver edits, event clears or RX flushes.
extern "C" void GpsBackupUartDiag_IRQHandler() {
  if (active && firstRx && NRF_UARTE0->EVENTS_ENDRX) {
    firstRx = false;
    GpsBackupUartDiag::capture(GpsBackupUartDiag::FirstRxIrq);
  }
  Serial1.IrqHandler();
}
#endif
