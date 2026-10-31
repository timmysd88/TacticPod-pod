#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <TinyGPS++.h>
#include <string.h>
#include "gps_uart_scan.h"
#include "gps_uart_config.h"
#include "recorder.h"
#include "flash_write_test.h"

namespace {
namespace Scan {
constexpr unsigned kRateCount = 6;
const uint32_t rates[kRateCount] = {4800, 9600, 19200, 38400, 57600, 115200};
const uint32_t baudRegisters[kRateCount] = {
  UARTE_BAUDRATE_BAUDRATE_Baud4800, UARTE_BAUDRATE_BAUDRATE_Baud9600,
  UARTE_BAUDRATE_BAUDRATE_Baud19200, UARTE_BAUDRATE_BAUDRATE_Baud38400,
  UARTE_BAUDRATE_BAUDRATE_Baud57600, UARTE_BAUDRATE_BAUDRATE_Baud115200
};
constexpr uint32_t kRxIrq = UARTE_INTENSET_ENDRX_Msk;
constexpr uint32_t kErrorMask = UARTE_ERRORSRC_FRAMING_Msk | UARTE_ERRORSRC_PARITY_Msk |
                               UARTE_ERRORSRC_OVERRUN_Msk | UARTE_ERRORSRC_BREAK_Msk;
enum class Phase { Idle, StopWait, StartWait, Measure, Report };
Phase phase = Phase::Idle;
struct Snapshot {
  uint32_t rx, tx, enabled, baud, config, started, stopped, ptr, maxcnt, irq;
};
Snapshot snapshot() {
  return {NRF_UARTE0->PSEL.RXD, NRF_UARTE0->PSEL.TXD, NRF_UARTE0->ENABLE,
          NRF_UARTE0->BAUDRATE, NRF_UARTE0->CONFIG, NRF_UARTE0->EVENTS_RXSTARTED,
          NRF_UARTE0->EVENTS_RXTO, NRF_UARTE0->RXD.PTR, NRF_UARTE0->RXD.MAXCNT,
          NRF_UARTE0->INTENSET & kRxIrq};
}
bool configured(const Snapshot& s, uint32_t baud) {
  return s.rx == 44 && s.tx == 43 && s.enabled == UARTE_ENABLE_ENABLE_Enabled &&
         s.baud == baud && s.ptr != 0 && s.maxcnt == 1 && s.irq != 0 &&
         s.started != 0 && s.stopped == 0;
}
struct Result {
  uint32_t bytes = 0, framing = 0, parity = 0, overrun = 0, breaks = 0;
  uint32_t valid = 0, elapsed = 0;
  bool tested = false;
  Snapshot hw = {};
};
Result results[kRateCount];
Snapshot before = {}, after = {};
TinyGPSPlus validator;
unsigned rateIndex = 0, reportLine = 0;
uint32_t phaseAt = 0, oldRxIrq = 0, discarded = 0;
bool restoring = false, restored = false, restoreConfirmed = false, locked = false, noticePending = false;
const char* error = nullptr;
char line[256];
size_t length = 0;

void text(const char* p) {
  while (*p && length + 1 < sizeof(line)) line[length++] = *p++;
  line[length] = 0;
}
void number(uint32_t value) {
  char digits[11]; char* p = digits + 10; *p = 0;
  do { *--p = '0' + value % 10; value /= 10; } while (value);
  text(p);
}
void hex(uint32_t value) {
  text("0x");
  for (int shift = 28; shift >= 0; shift -= 4) {
    char digit[2] = {"0123456789ABCDEF"[(value >> shift) & 15], 0}; text(digit);
  }
}
void clearErrors() {
  const uint32_t flags = NRF_UARTE0->ERRORSRC & kErrorMask;
  NRF_UARTE0->ERRORSRC = flags;  // Hardware write-one-to-clear, not ordinary RAM.
  NRF_UARTE0->EVENTS_ERROR = 0;
}
void countErrors() {
  const uint32_t flags = NRF_UARTE0->ERRORSRC & kErrorMask;
  Result& r = results[rateIndex];
  if (flags & UARTE_ERRORSRC_FRAMING_Msk) ++r.framing;
  if (flags & UARTE_ERRORSRC_PARITY_Msk) ++r.parity;
  if (flags & UARTE_ERRORSRC_OVERRUN_Msk) ++r.overrun;
  if (flags & UARTE_ERRORSRC_BREAK_Msk) ++r.breaks;
  clearErrors();
}
void startStop(bool restore) {
  restoring = restore;
  // Core ENDRX handler restarts RX automatically; mask it during STOPRX.
  // TX is idle and scan never writes UART, so no ENDTX IRQ can re-enter RX here.
  NRF_UARTE0->INTENCLR = kRxIrq;
  NRF_UARTE0->EVENTS_RXTO = 0;
  NRF_UARTE0->TASKS_STOPRX = 1;
  phase = Phase::StopWait; phaseAt = millis();
}
void report() {
  after = snapshot();
  restored = restoreConfirmed && configured(after, GpsUartConfig::kNormalBaudRegister);
  locked = !restored;
  phase = Phase::Report; reportLine = 0; length = 0; noticePending = false;
}
void abort(const char* why) {
  if (!error) error = why;
  if (!restoring) startStop(true);
  else {
    // Do not disable an unacknowledged receiver: report failure honestly.
    NRF_UARTE0->INTENSET = oldRxIrq;
    report();
  }
}
void showSnapshot(const Snapshot& s) {
  text(" RXD="); hex(s.rx); text(" TXD="); hex(s.tx);
  text(" ENABLE="); number(s.enabled); text(" BAUDREG="); hex(s.baud);
  text(" RXSTARTED="); number(s.started); text(" RXTO="); number(s.stopped);
  text(" DMA_PTR="); hex(s.ptr); text(" MAXCNT="); number(s.maxcnt);
  text(" RX_IRQ="); number(s.irq ? 1 : 0); text(" CONFIG="); hex(s.config);
}
void outputReport() {
  if (!Serial || Recorder::exporting()) return;
  if (!length) {
    if (reportLine == 0) { text("GPS SCAN BEFORE |"); showSnapshot(before); }
    else if (reportLine <= kRateCount * 2) {
      unsigned index = (reportLine - 1) / 2;
      const Result& r = results[index];
      text("GPS SCAN baud="); number(rates[index]);
      if ((reportLine - 1) % 2) { text(" |"); showSnapshot(r.hw); }
      else if (!r.tested) text(" | NOT TESTED");
      else {
        text(" | elapsed_ms="); number(r.elapsed); text(" | bytes="); number(r.bytes);
        text(" | valid_nmea="); number(r.valid);
        text(" | framing_flags="); number(r.framing); text(" | parity_flags="); number(r.parity);
        text((r.hw.config & UARTE_CONFIG_PARITY_Msk) ? "(enabled)" : "(disabled/8N1)");
        text(" | overrun_flags="); number(r.overrun); text(" | break_flags="); number(r.breaks);
      }
    } else if (reportLine == kRateCount * 2 + 1) { text("GPS SCAN AFTER |"); showSnapshot(after); }
    else if (reportLine == kRateCount * 2 + 2) {
      text("GPS SCAN: error counts are observed sticky flags (lower bounds); parity cannot be checked when disabled. Transition-discarded bytes="); number(discarded);
    } else {
      text("GPS SCAN END | restored_38400="); text(restored ? "YES" : "NO; reset required");
      text(" | RX expected D7/P1.12=44; TX D6/P1.11=43 | ");
      text(error ? error : "six receive-only windows complete; no GPS commands or flash writes");
    }
    text("\n");
  }
  if (Serial.availableForWrite() < int(length)) return;
  if (Serial.write(reinterpret_cast<const uint8_t*>(line), length) != length) return;
  length = 0;
  if (++reportLine > kRateCount * 2 + 3) phase = Phase::Idle;
}
}
}

namespace GpsUartScan {
bool active() { return Scan::phase != Scan::Phase::Idle; }
bool faulted() { return Scan::locked; }
bool ownsReceiver() {
  return Scan::locked || (Scan::phase != Scan::Phase::Idle && Scan::phase != Scan::Phase::Report);
}
bool start() {
  using namespace Scan;
  if (active() || locked || Recorder::exclusive() || !FlashWriteTest::diagnosticsAllowed()) return false;
  before = snapshot(); oldRxIrq = before.irq;
  for (auto& r : results) r = Result{};
  rateIndex = 0; discarded = 0; error = nullptr; restored = restoreConfirmed = false; noticePending = true;
  // Validate actual registers before changing anything, rather than just trusting defines.
  if (!configured(before, GpsUartConfig::kNormalBaudRegister) || !oldRxIrq) {
    error = "initial UART pins/receive/baud configuration unexpected; scan not started";
    report(); return true;
  }
  startStop(false); return true;
}
void update() {
  using namespace Scan;
  if (phase == Phase::Idle) return;
  if (phase == Phase::Report) { outputReport(); return; }
  if (noticePending && Serial) {
    const char notice[] = "GPS SCAN: receive-only, 6 rates x 3000 ms; recording must stay stopped. Restoring 38400 afterward.\n";
    if (Serial.availableForWrite() >= int(sizeof(notice) - 1)) {
      Serial.write(reinterpret_cast<const uint8_t*>(notice), sizeof(notice) - 1); noticePending = false;
    }
  }
  const uint32_t now = millis();
  if (phase == Phase::StopWait) {
    if (!NRF_UARTE0->EVENTS_RXTO) {
      if (uint32_t(now - phaseAt) >= 100) abort("RX stop timeout; scan aborted");
      return;
    }
    // RX stopped: no producer can add to the core's 64-byte RX ring now.
    for (unsigned i = 0; i < SERIAL_BUFFER_SIZE && Serial1.available(); ++i) { Serial1.read(); ++discarded; }
    if (NRF_UARTE0->EVENTS_ENDRX) discarded += NRF_UARTE0->RXD.AMOUNT;
    NRF_UARTE0->EVENTS_ENDRX = 0;
    NRF_UARTE0->ENABLE = UARTE_ENABLE_ENABLE_Disabled;
    NRF_UARTE0->BAUDRATE = restoring ? GpsUartConfig::kNormalBaudRegister : baudRegisters[rateIndex];
    clearErrors();
    NRF_UARTE0->EVENTS_RXTO = 0; NRF_UARTE0->EVENTS_RXSTARTED = 0;
    NRF_UARTE0->ENABLE = UARTE_ENABLE_ENABLE_Enabled;
    NRF_UARTE0->INTENSET = oldRxIrq;
    NRF_UARTE0->TASKS_STARTRX = 1;
    phase = Phase::StartWait; phaseAt = now;
  } else if (phase == Phase::StartWait) {
    if (!NRF_UARTE0->EVENTS_RXSTARTED) {
      if (uint32_t(now - phaseAt) >= 100) abort("RX start timeout; scan aborted");
      return;
    }
    if (restoring) { restoreConfirmed = true; report(); return; }
    Result& r = results[rateIndex]; r.hw = snapshot();
    if (!configured(r.hw, baudRegisters[rateIndex])) { abort("rate configuration/receive validation failed"); return; }
    validator = TinyGPSPlus(); clearErrors(); r.tested = true;
    phaseAt = now; phase = Phase::Measure;
  } else if (phase == Phase::Measure) {
    countErrors();
    Result& r = results[rateIndex];
    if (uint32_t(now - phaseAt) >= 3000) {
      r.elapsed = now - phaseAt;
      if (++rateIndex == kRateCount) startStop(true); else startStop(false);
      return;
    }
    for (unsigned budget = 0; budget < 64 && Serial1.available() && uint32_t(millis() - phaseAt) < 3000; ++budget) {
      const int value = Serial1.read();
      if (value < 0) break;
      ++r.bytes; if (validator.encode(char(value))) ++r.valid;
    }
  }
}
}
