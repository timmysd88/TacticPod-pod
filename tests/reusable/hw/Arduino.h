#pragma once
#include <stdint.h>
#include "../../../.pio-core/packages/framework-arduinoadafruitnrf52/cores/nRF5/nordic/nrfx/mdk/nrf52840_bitfields.h"
#define LOW 0
#define HIGH 1
#define OUTPUT 1
#define INPUT 0
#define VBAT_ENABLE 14
#define PIN_VBAT 32
#define PIN_CHARGING_CURRENT 22
extern uint8_t writes[33],modes[33];
inline void digitalWrite(unsigned p,unsigned n){writes[p]=n;}
inline void pinMode(unsigned p,unsigned n){modes[p]=n;}
struct Adc {
  uint32_t ENABLE=0,INTENSET=0,RESOLUTION=0,OVERSAMPLE=0,SAMPLERATE=0;
  struct Channel {uint32_t PSELP=0,PSELN=0,CONFIG=0;} CH[8];
  struct Result {uint32_t PTR=0,MAXCNT=0,AMOUNT=0;} RESULT;
  uint32_t EVENTS_STARTED=0,EVENTS_END=0,EVENTS_STOPPED=0,EVENTS_CALIBRATEDONE=0;
  uint32_t TASKS_START=0,TASKS_SAMPLE=0,TASKS_STOP=0,TASKS_CALIBRATEOFFSET=0;
};
extern Adc adc;
struct Power {uint32_t USBREGSTATUS=0;};extern Power power;
#define NRF_SAADC (&adc)
#define NRF_POWER (&power)
