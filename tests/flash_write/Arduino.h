#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#define PIN_A0 0
#define INPUT_PULLUP 2
#define PIN_SERIAL1_RX 7
#define PIN_SERIAL1_TX 6
typedef uint8_t byte;
#define radians(n) ((n) * 0.017453292519943295)
#define degrees(n) ((n) * 57.29577951308232)
#define sq(n) ((n) * (n))
#define TWO_PI 6.283185307179586
#define SERIAL_BUFFER_SIZE 64
#include "../../.pio-core/packages/framework-arduinoadafruitnrf52/cores/nRF5/nordic/nrfx/mdk/nrf52840_bitfields.h"
void mockStopRx();
void mockStartRx();
struct MockTask {
  void (*callback)();
  MockTask(void (*fn)()) : callback(fn) {}
  void operator=(uint32_t value) { if (value) callback(); }
};
struct MockW1C {
  uint32_t bits = 0;
  operator uint32_t() const { return bits; }
  void operator=(uint32_t mask) { bits &= ~mask; }
};
struct MockUarte {
  struct { uint32_t AMOUNT = 0; } TXD;
  struct { uint32_t PTR = 0, MAXCNT = 0, AMOUNT = 0; } RXD;
  struct { uint32_t RXD = 0, TXD = 0; } PSEL;
  uint32_t EVENTS_ENDTX = 0, EVENTS_ENDRX = 0, EVENTS_RXSTARTED = 0, EVENTS_RXTO = 0, EVENTS_ERROR = 0;
  uint32_t ENABLE = 0, BAUDRATE = 0, CONFIG = 0, INTENSET = 0, INTENCLR = 0;
  MockW1C ERRORSRC;
  MockTask TASKS_STOPRX{mockStopRx}, TASKS_STARTRX{mockStartRx};
  bool stuckStop = false, stuckStart = false;
  unsigned stopCalls = 0, startCalls = 0;
};
extern MockUarte mockUarte;
#define NRF_UARTE0 (&mockUarte)
#define HEX 16
#define LOW 0
#define HIGH 1
#define OUTPUT 1
#define LED_RED 11
#define LED_GREEN 13
#define LED_BLUE 12
extern uint32_t mockTime;
extern unsigned mockRedWrites;
inline unsigned long millis() { return mockTime; }
// TIMER4 register model shared by all ARM regression harnesses. Capture uses
// simulated time only; new tests can inject sub-millisecond and wrap races.
static uint32_t mockSubUs = 0, mockTimerOriginMs = 0, mockTimerLastLow = 0;
static uint32_t mockPrimask = 0;
static bool mockWrapOnCapture = false;
void mockCaptureClock();
void mockClearClock();
struct MockClockCapture { void operator=(uint32_t value) { if (value) mockCaptureClock(); } };
struct MockClockClear { void operator=(uint32_t value) { if (value) mockClearClock(); } };
struct MockTimer {
  uint32_t TASKS_STOP = 0, MODE = 0, BITMODE = 0, PRESCALER = 0, SHORTS = 0;
  uint32_t INTENCLR = 0, INTENSET = 0, TASKS_START = 0;
  MockClockClear TASKS_CLEAR;
  uint32_t CC[2] = {}, EVENTS_COMPARE[2] = {};
  MockClockCapture TASKS_CAPTURE[2];
};
static MockTimer mockClockTimer;
inline void mockClearClock() {
  mockTimerOriginMs = mockTime; mockTimerLastLow = 0;
  mockClockTimer.CC[1] = 0; mockClockTimer.EVENTS_COMPARE[0] = 0;
}
inline void mockCaptureClock() {
  uint32_t low = uint32_t(uint32_t(mockTime - mockTimerOriginMs) * 1000 + mockSubUs);
  if (low < mockTimerLastLow) mockClockTimer.EVENTS_COMPARE[0] = 1;
  if (mockWrapOnCapture) {
    mockWrapOnCapture = false; mockClockTimer.EVENTS_COMPARE[0] = 1;
    mockClockTimer.CC[1] = 0xffffffffUL; mockTimerLastLow = 0;
  } else { mockClockTimer.CC[1] = low; mockTimerLastLow = low; }
}
#define NRF_TIMER4 (&mockClockTimer)
#define TIMER4_IRQn 4
inline void NVIC_ClearPendingIRQ(int) {}
inline void NVIC_SetPriority(int, int) {}
inline void NVIC_EnableIRQ(int) {}
inline uint32_t __get_PRIMASK() { return mockPrimask; }
inline void __disable_irq() { mockPrimask = 1; }
inline void __enable_irq() { mockPrimask = 0; }
static uint8_t mockPinLevels[33] = {};
inline void digitalWrite(uint8_t pin, uint8_t value) { if(pin<33)mockPinLevels[pin]=value; if (pin == LED_RED) ++mockRedWrites; }
inline void pinMode(uint8_t, uint8_t) {}
// Host model of D0: LOW when pressed. delay() only advances simulated time.
static bool mockButtonLow = false;
static unsigned mockDelayCalls = 0, mockSerialPins = 0;
inline int digitalRead(uint8_t pin) { return pin == PIN_A0 && mockButtonLow ? LOW : HIGH; }
inline void delay(unsigned long ms) { mockTime += uint32_t(ms); ++mockDelayCalls; }
inline void delayMicroseconds(unsigned us) { mockSubUs += us; mockTime += mockSubUs / 1000; mockSubUs %= 1000; }
struct MockSerial {
  bool connected = true;
  const char* rx = "";
  char output[262144] = {};  // Test-only sink for a full 48 KiB escaped RAW dump.
  size_t used = 0;
  int freeSpace = 256;
  unsigned overfilledWrites = 0;
  bool consumeWriteSpace=false,disconnectOnWrite=false;
  size_t writeLimit=~size_t(0);
  unsigned writeCalls=0;
  explicit operator bool() const { return connected; }
  int available() { return *rx != 0; }
  int read() { return *rx ? *rx++ : -1; }
  void begin(unsigned long) {}
  int availableForWrite() { return freeSpace; }
  size_t write(const uint8_t* data, size_t length) {
    ++writeCalls;
    if (length > size_t(freeSpace)) { ++overfilledWrites; return 0; }
    if(length>writeLimit)length=writeLimit;
    if(consumeWriteSpace)freeSpace-=int(length);
    for (size_t i = 0; i < length && used + 1 < sizeof(output); ++i) output[used++] = char(data[i]);
    output[used] = 0;
#ifdef TCB_SIM_USB_WRITE
    TCB_SIM_USB_WRITE(data,length);
#endif
    if(disconnectOnWrite)connected=false;
    return length;
  }
  void print(float value, int digits = 2) { printFixed(value, digits); }
  void print(double value, int digits = 2) { printFixed(value, digits); }
  void println(float value, int digits) { print(value, digits); println(); }
  void print(const char* text) { while (*text && used + 1 < sizeof(output)) output[used++] = *text++; output[used] = 0; }
  void print(char* text) { print(static_cast<const char*>(text)); }
  void print(char value) { char text[2] = {value, 0}; print(text); }
  template<typename T> void print(T value, int base = 10) {
    char text[32]; char* cursor = text + 31; *cursor = 0;
    do { *--cursor = "0123456789ABCDEF"[value % base]; value /= base; } while (value);
    print(cursor);
  }
  void printFixed(double value, int digits) {
    if (value < 0) { print('-'); value = -value; }
    uint32_t scale = 1;
    for (int i = 0; i < digits; ++i) scale *= 10;
    const uint32_t rounded = uint32_t(value * scale + 0.5);
    print(rounded / scale);
    if (digits > 0) {
      print('.');
      uint32_t remainder = rounded % scale;
      for (uint32_t place = scale / 10; place; place /= 10) {
        print((remainder / place) % 10);
      }
    }
  }
  void println() { print('\n'); }
  template<typename T> void println(T value) { print(value); println(); }
  template<typename T> void println(T value, int base) { print(value, base); println(); }
};
extern MockSerial Serial;
struct MockUart {
  const char* rx = "";
  const uint8_t* binary = nullptr;
  size_t binaryRemaining = 0;
  unsigned long baud = 0;
  bool loopEcho = false, corruptEcho = false, stuckTx = false, shortTx = false;
  unsigned writeCalls = 0;
  uint8_t echo[64];
  uint8_t writes[256][64] = {};
  size_t writeSizes[256] = {};
  unsigned endCalls = 0, beginCalls = 0;
  void end() {
    ++endCalls; baud = 0; mockUarte.ENABLE = 0; mockUarte.RXD.AMOUNT = 0;
    mockUarte.PSEL.RXD = 0xFFFFFFFFUL; mockUarte.PSEL.TXD = 0xFFFFFFFFUL;
  }
  void begin(unsigned long value) {
    ++beginCalls;
    baud = value; mockUarte.ENABLE = 8;
    mockUarte.BAUDRATE = value == 38400 ? UARTE_BAUDRATE_BAUDRATE_Baud38400 : UARTE_BAUDRATE_BAUDRATE_Baud9600;
    mockUarte.PSEL.RXD = 44; mockUarte.PSEL.TXD = 43;
    mockUarte.RXD.PTR = 0x20001000; mockUarte.RXD.MAXCNT = 1;
    mockUarte.EVENTS_RXSTARTED = 1; mockUarte.EVENTS_RXTO = 0;
    mockUarte.INTENSET = UARTE_INTENSET_ENDRX_Msk | UARTE_INTENSET_ENDTX_Msk;
  }
  int available() { return binaryRemaining || *rx != 0; }
  int read() {
    if (binaryRemaining) { --binaryRemaining; return *binary++; }
    return *rx ? *rx++ : -1;
  }
  size_t write(const uint8_t* data, size_t count) {
    if (writeCalls < 256 && count <= 64) { memcpy(writes[writeCalls], data, count); writeSizes[writeCalls] = count; }
    ++writeCalls;
    if (!stuckTx) mockUarte.TXD.AMOUNT = count;
    if (loopEcho) {
      memcpy(echo, data, count); if (corruptEcho) echo[3] ^= 1;
      binary = echo; binaryRemaining = count;
    }
    return shortTx ? count - 1 : count;
  }
};
extern MockUart Serial1;
