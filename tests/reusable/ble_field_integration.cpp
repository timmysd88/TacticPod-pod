#define TACTICPOD_BLE 1
#include "field_integration.cpp"
#include "../../src/ble_phase1.h"

struct FieldBleAdapter {
  unsigned begins = 0, stops = 0, resets = 0;
  unsigned aborts = 0;
  bool link = false, activity = false, fault = false, entrySafe = false;
  bool begin() {
    ++begins;
    entrySafe = suspended && MonotonicClock::suspended() &&
      !Tcb3Session::exclusive() && !Recorder::exclusive() && !CompactRecorder::capturing();
    return entrySafe;
  }
  void service(uint32_t) {}
  bool connected() { return link; }
  bool takeActivity() { bool v = activity; activity = false; return v; }
  bool transferActive() { return false; }
  bool takeProgress() { return false; }
  void abortTransfer() { ++aborts; }
  bool failed() { return fault; }
  void stop() { ++stops; link = false; }
  bool quiescent() { return !link; }
  void reset() { ++resets; }
};
FieldBleAdapter fieldBle;
BlePhase1::Runtime<FieldBleAdapter> fieldBleRuntime;
namespace BleRuntime {
bool active() { return fieldBleRuntime.active(); }
void update(uint32_t now, const BleMode::Inputs& in) { fieldBleRuntime.update(now, in, fieldBle); }
StatusLed::Color ledColor(uint32_t now) { return fieldBleRuntime.ledColor(now); }
bool returnedFromBle() { return false; }
}

extern "C" unsigned bleFieldCase(unsigned c) {
  // Run the existing real-main recording/stop path with the opt-in hooks enabled.
  if (c == 0) {
    const unsigned result = fieldCase(0);
    return result ? result : fieldBle.begins ? 100 : 0;
  }
  Serial.connected = c != 5;
  setup(); startupPending = false; waitIdle(); runMs(3500);
  if (c == 5 && !standbyPolicy.inStandby()) return 1;
  // Deliberate taps exercise the actual 50/10 ms polling and release debounce.
  mockButtonLow = true; runMs(120);
  mockButtonLow = false; runMs(150);
  mockButtonLow = true; runMs(120);
  mockButtonLow = false; runMs(100);
  if (fieldBle.begins != 1 || !fieldBle.entrySafe || !BleRuntime::active()) return 2;
  if (Tcb3Session::recording() || Tcb3Session::exclusive() || SPI.programs || SPI.erases) return 3;
  if (Tcb3Session::startFromButton()) return 4;
  Tcb3Session::handleCommand("TCB START");
  Tcb3Session::handleCommand("TCB READ");
  if (Tcb3Session::exclusive() || Tcb3Session::exporting()) return 5;
  if (c == 1 || c == 5) {
    runMs(180100);
  } else if (c == 2) {
    mockButtonLow = true; runMs(3200);
  } else {
    fieldBle.link = true; runMs(20);
    mockButtonLow = true; runMs(3200); mockButtonLow = false;
    if (fieldBle.stops || fieldBle.resets) return 6;
    runMs(180000); // Connected devices are not limited by the advertising timeout.
    if (fieldBle.stops || fieldBle.resets) return 7;
    if (c == 3) {
      fieldBle.activity = true; runMs(20);
      runMs(299000); if (fieldBle.stops) return 8;
      runMs(1100);
    } else {
      fieldBle.link = false; runMs(30);
    }
  }
  if (fieldBle.stops != 1 || fieldBle.resets != 1) return 9;
  if (Tcb3Session::recording() || SPI.programs || SPI.erases) return 10;
  return mockPinLevels[LED_RED] == LOW || mockPinLevels[LED_GREEN] == LOW ||
         mockPinLevels[LED_BLUE] == LOW ? 11 : 0;
}
