#include <Arduino.h>
#include "monotonic_clock.h"

namespace {
volatile uint32_t clockHigh = 0;
uint64_t clockOrigin = 0;
bool clockStarted = false;
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
}
