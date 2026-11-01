#include <Arduino.h>
#include "monotonic_clock.h"

namespace {
volatile uint32_t clockHigh = 0;
uint64_t clockOrigin = 0;
bool clockStarted = false;
bool clockSuspended = false;
uint64_t suspendedAtUs = 0;
uint32_t suspendedAtMs = 0;
void overflow() {
  if (NRF_TIMER4->EVENTS_COMPARE[0]) {
    NRF_TIMER4->EVENTS_COMPARE[0] = 0;
    (void)NRF_TIMER4->EVENTS_COMPARE[0]; // Complete the peripheral clear before IRQ return.
    ++clockHigh;
  }
}
}

extern "C" void TIMER4_IRQHandler() { overflow(); }

namespace MonotonicClock {
void begin() {
  if (clockStarted) return;
  // Preserve the early boot offset; do not use the BSP's DWT-dependent micros().
  clockOrigin = uint64_t(millis()) * 1000;
  NRF_TIMER4->TASKS_STOP = 1;
  NRF_TIMER4->MODE = 0;       // TIMER
  NRF_TIMER4->BITMODE = 3;    // 32 bits
  NRF_TIMER4->PRESCALER = 4;  // 16MHz / 16 = 1MHz
  NRF_TIMER4->SHORTS = 0;
  NRF_TIMER4->INTENCLR = 0xffffffffUL;
  NRF_TIMER4->TASKS_CLEAR = 1;
  NRF_TIMER4->CC[0] = 0;     // Compare at wrap, not UINT32_MAX (one tick early).
  NRF_TIMER4->EVENTS_COMPARE[0] = 0;
  clockHigh = 0;
  NVIC_ClearPendingIRQ(TIMER4_IRQn);
  NVIC_SetPriority(TIMER4_IRQn, 7);
  NRF_TIMER4->INTENSET = TIMER_INTENSET_COMPARE0_Msk;
  NVIC_EnableIRQ(TIMER4_IRQn);
  clockStarted = true;
  NRF_TIMER4->TASKS_START = 1;
}
uint64_t now() {
  begin();
  // Standby: TIMER4 is stopped; continue from the RTC-driven millisecond tick.
  if (clockSuspended) return suspendedAtUs + uint64_t(uint32_t(millis() - suspendedAtMs)) * 1000;
  // Prevent the ISR changing the upper word while capturing the lower word.
  // Also service a pending wrap: capture may race the compare interrupt.
  const uint32_t mask = __get_PRIMASK();
  __disable_irq();
  NRF_TIMER4->TASKS_CAPTURE[1] = 1;
  uint32_t low = NRF_TIMER4->CC[1];
  if (NRF_TIMER4->EVENTS_COMPARE[0]) {
    overflow();
    NRF_TIMER4->TASKS_CAPTURE[1] = 1;
    low = NRF_TIMER4->CC[1];
  }
  const uint64_t value = clockOrigin + (uint64_t(clockHigh) << 32) + low;
  if (!mask) __enable_irq();
  return value;
}
// Stop the 1 MHz TIMER4 for standby. now() stays monotonic at millisecond resolution.
void suspend() {
  begin();
  if (clockSuspended) return;
  suspendedAtUs = now();
  suspendedAtMs = millis();
  NRF_TIMER4->TASKS_STOP = 1;
  NRF_TIMER4->INTENCLR = TIMER_INTENCLR_COMPARE0_Msk;
  clockSuspended = true;
}
void resume() {
  if (!clockSuspended) return;
  const uint64_t value = now();
  NRF_TIMER4->TASKS_STOP = 1;
  NRF_TIMER4->TASKS_CLEAR = 1;
  NRF_TIMER4->EVENTS_COMPARE[0] = 0;
  clockOrigin = value;
  clockHigh = 0;
  NVIC_ClearPendingIRQ(TIMER4_IRQn);
  NRF_TIMER4->INTENSET = TIMER_INTENSET_COMPARE0_Msk;
  clockSuspended = false;
  NRF_TIMER4->TASKS_START = 1;
}
bool suspended() { return clockSuspended; }
}
