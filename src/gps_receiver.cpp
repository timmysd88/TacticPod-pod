#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <TinyGPS++.h>
#include <string.h>
#include "gps_receiver.h"
#include "recorder.h"
#include "gps_uart_scan.h"
#include "gps_uart_config.h"
#include "gps_rate.h"
#include "gps_config.h"
#include "gps_uart_tx.h"
#include "gps_profile.h"
#include "ram_observations.h"
#include "compact_recorder.h"
#include "tcb3_session.h"
#include "gps_power.h"

// Verified in Seeed_XIAO_nRF52840/variant.h and cores/nRF5/Uart.cpp:
// Serial1 = UARTE0, RX = D7/P1.12, TX = D6/P1.11.
static_assert(PIN_SERIAL1_RX == 7 && PIN_SERIAL1_TX == 6,
              "GPS wiring requires the XIAO D7 RX / D6 TX UART variant");

namespace {
constexpr uint32_t kFreshMs = 5000;
TinyGPSPlus gps;
TinyGPSCustom gpQuality(gps, "GPGGA", 6), gnQuality(gps, "GNGGA", 6);
TinyGPSCustom gpRmc(gps, "GPRMC", 2), gnRmc(gps, "GNRMC", 2);
TinyGPSCustom gpHdop(gps, "GPGGA", 8), gnHdop(gps, "GNGGA", 8);
TinyGPSCustom gpSats(gps, "GPGGA", 7), gnSats(gps, "GNGGA", 7);
bool suspended = false;
#if TACTICPOD_GPS_BACKUP_STANDBY || TACTICPOD_GPS_BACKUP_TEST
void writeWakeBytes() {
  static const uint8_t ff[GpsPower::kWakeByteCount] = {0xFF, 0xFF, 0xFF, 0xFF};
  GpsUartTx::write(ff, sizeof(ff));
}
#endif
#if TACTICPOD_GPS_BACKUP_TEST
GpsPower::BackupTest backupTest;
bool backupReported = false, backupLatched = false;
#endif
// The supervised test owns the UART while it runs; automatic wake must stay out of it.
bool backupTestRunning() {
#if TACTICPOD_GPS_BACKUP_TEST
  return backupTest.running();
#else
  return false;
#endif
}
#if TACTICPOD_GPS_BACKUP_STANDBY
bool wakePending = false;
unsigned wakeAttempts = 0;
uint32_t lastWakeAt = 0;
void sendWakeBytes() {
  writeWakeBytes();
  ++wakeAttempts; lastWakeAt = millis();
}
#endif
bool printing = false, receivedByte = false, receivedValid = false;
bool startupPending = false, startupNoCommunication = false;
#if TACTICPOD_REUSABLE
bool profileRequested=false, profileRunning=false, profileFailed=false;
uint32_t profileRequestedAt=0, lastRmcMs=0;
unsigned slowRmc=0;
#endif
bool fixKnown = false, fixReported = false, hdopPresent = false, satsPresent = false;
uint32_t startedAt = 0, lastByteAt = 0, lastValidAt = 0, lastFixAt = 0;
uint32_t byteCount = 0, lastReportAt = 0;
char output[1280];
size_t outputLength = 0, outputPosition = 0;
constexpr uint32_t kRawDurationMs = 10000;
constexpr size_t kRawCapacity = GpsUartConfig::kRawBufferBytes;
enum class RawPhase { Idle, Capture, Dump, Summary };
RawPhase rawPhase = RawPhase::Idle;
uint8_t rawBytes[kRawCapacity];
uint32_t rawStartedAt = 0, rawTotal = 0, rawValidSentences = 0;
size_t rawStored = 0, rawCursor = 0, rawPendingAdvance = 0;
TinyGPSPlus rawValidator;  // Fresh state prevents pre-capture fragments counting.
// Not a UBX packet or NMEA command. Disconnect GPS UART wires and bridge D6-D7
// before explicitly running this temporary hardware diagnostic.
const uint8_t kLoopPattern[] = {
  'T','A','C','T','I','C','P','O','D','_','U','A','R','T','_','9','6','0','0',
  0x00, 0x55, 0xAA, 0xFF, 0x01, 0x7F, 0x0D, 0x0A, 0x33, 0xCC, 0x5A, 0xA5, '!'
};
// The legacy "9600" in this fixed pattern is test data, not a baud setting.
static_assert(sizeof(kLoopPattern) <= SERIAL_BUFFER_SIZE, "One UART DMA batch only");
enum class LoopPhase { Idle, Quiet, Receive, Report };
LoopPhase loopPhase = LoopPhase::Idle;
uint32_t loopStartedAt = 0, loopSent = 0, loopReceived = 0, loopMismatches = 0, loopDiscarded = 0;
bool loopPassed = false;
const char* loopReason = "";

bool uartTxReady() {
  return GpsUartTx::ready();
}

void text(const char* value) {
  while (*value && outputLength + 1 < sizeof(output)) output[outputLength++] = *value++;
  output[outputLength] = 0;
}
void number(uint32_t value) {
  char digits[11]; char* end = digits + 10; *end = 0;
  do { *--end = '0' + value % 10; value /= 10; } while (value);
  text(end);
}
void decimal(double value, unsigned places) {
  if (value < 0) { text("-"); value = -value; }
  uint32_t scale = 1;
  for (unsigned i = 0; i < places; ++i) scale *= 10;
  const uint32_t scaled = uint32_t(value * scale + 0.5);
  number(scaled / scale); text(".");
  for (uint32_t divisor = scale / 10; divisor; divisor /= 10) {
    char digit[2] = {char('0' + (scaled / divisor) % 10), 0}; text(digit);
  }
}
void twoDigits(unsigned value) { if (value < 10) text("0"); number(value); }
bool fresh(bool valid, uint32_t age) { return valid && age <= kFreshMs; }
bool liveFix() {
  const uint32_t now = millis();
  const bool nmeaFresh = receivedValid && uint32_t(now - lastValidAt) <= kFreshMs;
  const bool positionFresh = fresh(gps.location.isValid(), gps.location.age());
  return nmeaFresh && positionFresh && fixKnown && fixReported &&
    uint32_t(now - lastFixAt) <= kFreshMs;
}

void updateFix(TinyGPSCustom& field, bool rmc) {
  if (!field.isUpdated()) return;
  const char* value = field.value();
  fixKnown = *value != 0;
  // RMC 'V' and GGA quality 0 explicitly invalidate an older cached position.
  fixReported = rmc ? value[0] == 'A' : value[0] >= '1' && value[0] <= '5';
  lastFixAt = millis();
}
void pollUart() {
  // Bound parser work each loop. No waiting for characters or sentence completion.
  for (unsigned budget = 0; budget < 64 && Serial1.available(); ++budget) {
    const int value = Serial1.read();
    if (value < 0) break;
    if (loopPhase == LoopPhase::Quiet) {
      ++loopDiscarded;  // Count pre-existing/noisy input instead of hiding it.
      continue;
    }
    if (loopPhase == LoopPhase::Receive) {
      if (loopReceived >= sizeof(kLoopPattern) || uint8_t(value) != kLoopPattern[loopReceived]) ++loopMismatches;
      ++loopReceived;
      continue;  // Test echoes must not pollute GPS/parser/capture statistics.
    }
    const bool ubx = GpsConfig::consume(uint8_t(value));
    GpsRate::observe(uint8_t(value));
    // Tap the single UART reader BEFORE the normal parser. Every byte is
    // inspected, including NUL/binary/control bytes; neither parser owns RX.
    if (rawPhase == RawPhase::Capture && uint32_t(millis() - rawStartedAt) < kRawDurationMs) {
      ++rawTotal;
      if (rawStored < kRawCapacity) rawBytes[rawStored++] = uint8_t(value);
      if (!ubx && rawValidator.encode(char(value))) ++rawValidSentences;
    }
    if (ubx) { RamObservations::interruptFrame(); continue; }
    RamObservations::observe(uint8_t(value));
    receivedByte = true; lastByteAt = millis(); ++byteCount;
    if (gps.encode(char(value))) {
      receivedValid = true; lastValidAt = millis();
#if TACTICPOD_REUSABLE
      if(gpRmc.isUpdated() || gnRmc.isUpdated()) {
        const uint32_t gap=millis()-lastRmcMs;
        if(lastRmcMs && gap>250)++slowRmc;else slowRmc=0;
        lastRmcMs=millis();
        if(slowRmc==3 && GpsProfile::currentVerified()) {
          GpsProfile::invalidate("UNVERIFIED (RMC cadence lost; RAM audit required)");
          profileRequested=true;profileRequestedAt=millis();
        }
      }
#endif
      updateFix(gpQuality, false); updateFix(gnQuality, false);
      updateFix(gpRmc, true); updateFix(gnRmc, true);
      if (gpHdop.isUpdated()) hdopPresent = *gpHdop.value() != 0;
      if (gnHdop.isUpdated()) hdopPresent = *gnHdop.value() != 0;
      if (gpSats.isUpdated()) satsPresent = *gpSats.value() != 0;
      if (gnSats.isUpdated()) satsPresent = *gnSats.value() != 0;
    }
  }
}
void buildStatus() {
  const uint32_t now = millis();
  outputLength = outputPosition = 0;
  const bool nmeaFresh = receivedValid && uint32_t(now - lastValidAt) <= kFreshMs;
  const bool hasFix = liveFix();
  text("GPS STATUS | UART=Serial1 RX:D7 TX:D6 38400 | NMEA=");
  text(nmeaFresh ? "VALID" : receivedValid ? "STALE" : "NONE");
  text(" | bytes="); number(byteCount);
  text(" | checksum_ok="); number(gps.passedChecksum());
  text(" | checksum_fail="); number(gps.failedChecksum());
  text(" | last_valid_message_age_ms=");
  if (receivedValid) number(now - lastValidAt); else text("N/A");
  text("\n");
  text("GPS configuration: "); text(GpsProfile::status()); text("\n");
  text("GPS last applied profile (historical): "); text(GpsProfile::lastApplied());text("\n");
  text("GPS RAM readback t_ms=");number(GpsProfile::checkedAt());text(" (snapshot, not proof of navigation cadence)\n");
  if (startupPending) text("GPS startup: waiting for valid NMEA and idle recording/diagnostics.\n");
  if (startupNoCommunication) text("GPS startup ERROR: no checksum-valid NMEA within 30s; settings unchanged.\n");
  if (*GpsProfile::error()) { text("GPS profile detail: "); text(GpsProfile::error()); text("\n"); }
  if (!receivedByte) {
    text(uint32_t(now - startedAt) >= kFreshMs ? "GPS ERROR: no UART data. " : "GPS waiting for UART data. ");
    text("Check GPS TX(T)->D7, GND, breakout supply and 38400 baud; no receiver configuration was sent.\n");
  } else if (uint32_t(now - lastByteAt) > kFreshMs) {
    text("GPS ERROR: UART data stopped; check power and TX wiring.\n");
  } else if (!nmeaFresh) {
    text("GPS ERROR: UART bytes received but no recent checksum-valid NMEA; check baud/output protocol.\n");
  }
  text("GPS fix=");
  text(hasFix ? "VALID" : !nmeaFresh ? "STALE/UNKNOWN" : fixKnown ? "NO FIX" : "UNKNOWN");
  text(" | lat=");
  if (gps.location.isValid()) decimal(gps.location.lat(), 6); else text("N/A");
  text(" | lon=");
  if (gps.location.isValid()) decimal(gps.location.lng(), 6); else text("N/A");
  text(" | position="); text(hasFix ? "CURRENT" : "LAST_KNOWN/UNAVAILABLE");
  text(" | satellites=");
  if (satsPresent && fresh(gps.satellites.isValid(), gps.satellites.age())) number(gps.satellites.value()); else text("N/A");
  text(" | HDOP=");
  if (hdopPresent && fresh(gps.hdop.isValid(), gps.hdop.age())) decimal(gps.hdop.hdop(), 2); else text("N/A");
  text("\nGPS speed_km/h=");
  if (hasFix && fresh(gps.speed.isValid(), gps.speed.age())) decimal(gps.speed.kmph(), 3); else text("N/A");
  text(" | UTC=");
  if (fresh(gps.time.isValid(), gps.time.age())) {
    twoDigits(gps.time.hour()); text(":"); twoDigits(gps.time.minute()); text(":"); twoDigits(gps.time.second()); text("Z");
  } else text("N/A");
  text(" | position_age_ms=");
  if (gps.location.isValid()) number(gps.location.age()); else text("N/A");
  text("\n");
  lastReportAt = now;
}
void flushOutput() {
  if (outputPosition == outputLength) return;
  size_t end = outputPosition;
  while (end < outputLength && output[end++] != '\n') {}
  const size_t length = end - outputPosition;
  // Every status line fits the core's 256-byte USB TX FIFO. Write only when
  // the entire line fits, so slow/disconnected USB never causes a wait loop.
  if (Serial.availableForWrite() < int(length)) return;
  outputPosition += Serial.write(reinterpret_cast<const uint8_t*>(output + outputPosition), length);
}

void serviceRawOutput() {
  if (rawPhase == RawPhase::Capture) {
    flushOutput();  // Nonblocking start acknowledgement; byte capture is RAM-only.
    return;
  }
  if (outputPosition == outputLength) {
    outputLength = outputPosition = 0;
    rawPendingAdvance = 0;
    if (rawPhase == RawPhase::Dump && rawCursor == rawStored) rawPhase = RawPhase::Summary;
    if (rawPhase == RawPhase::Dump) {
      text("GPS RAW | ");
      // Whole readable NMEA lines normally fit; limit binary expansion so each
      // output line remains below the existing 256-byte USB FIFO capacity.
      while (rawCursor + rawPendingAdvance < rawStored && outputLength < 186) {
        const uint8_t value = rawBytes[rawCursor + rawPendingAdvance++];
        if (value >= 0x20 && value <= 0x7E) {
          char literal[2] = {char(value), 0};
          if (value == '\\') text("\\");  // Escape literal backslashes too.
          text(literal);
        } else {
          const char hex[] = "0123456789ABCDEF";
          char escaped[5] = {'\\', 'x', hex[value >> 4], hex[value & 15], 0};
          text(escaped);
        }
        if (value == '\n') break;
      }
      text("\n");
    } else if (rawPhase == RawPhase::Summary) {
      text("GPS RAW END | duration_ms="); number(kRawDurationMs);
      text(" | total_bytes="); number(rawTotal);
      text(" | stored_bytes="); number(rawStored);
      text(" | omitted_bytes="); number(rawTotal - rawStored);
      text(" | valid_nmea="); text(rawValidSentences ? "YES" : "NO");
      text(" | valid_sentences="); number(rawValidSentences);
      if (rawTotal == 0) text(" | no UART data: check GPS TX->D7, power/GND and 38400 baud");
      text("\n");
    }
  }
  flushOutput();
  if (outputPosition == outputLength) {
    if (rawPhase == RawPhase::Dump) {
      rawCursor += rawPendingAdvance;
      rawPendingAdvance = 0;
    } else if (rawPhase == RawPhase::Summary) {
      rawPhase = RawPhase::Idle;
      lastReportAt = millis();  // Resume any previously enabled 1 Hz GPS output.
    }
  }
}

void finishLoopback(const char* reason) {
  loopPassed = loopSent == sizeof(kLoopPattern) && loopReceived == sizeof(kLoopPattern) &&
               loopMismatches == 0 && loopDiscarded == 0;
  loopReason = reason;
  loopPhase = LoopPhase::Report;
  outputLength = outputPosition = 0;
}

void serviceLoopback() {
  const uint32_t now = millis();
  if (loopPhase == LoopPhase::Quiet && uint32_t(now - loopStartedAt) >= 50) {
    if (loopDiscarded) { finishLoopback("RX not quiet; disconnect GPS and bridge D6-D7, then retry"); return; }
    if (!uartTxReady()) { finishLoopback("previous UART TX not complete; reset board before retrying"); return; }
    // One DMA-sized write, no flush(), no per-byte wait, no UART reinitialisation.
    loopSent = GpsUartTx::write(kLoopPattern, sizeof(kLoopPattern));
    loopStartedAt = now;
    loopPhase = LoopPhase::Receive;
    if (loopSent != sizeof(kLoopPattern)) finishLoopback("short UART write");
  } else if (loopPhase == LoopPhase::Receive && uint32_t(now - loopStartedAt) >= 1000) {
    finishLoopback(loopReceived == sizeof(kLoopPattern) && loopMismatches == 0 ?
                   "pattern matched" : "RX timeout/mismatch; check isolated D6-D7 bridge");
  }
}

void reportLoopback() {
  if (loopPhase != LoopPhase::Report) { flushOutput(); return; }
  if (outputPosition == outputLength) {
    outputLength = outputPosition = 0;
    text("UART LOOPBACK TEST "); text(loopPassed ? "PASS" : "FAIL");
    text(" | expected="); number(sizeof(kLoopPattern));
    text(" | tx_bytes="); number(loopSent);
    text(" | rx_bytes="); number(loopReceived);
    text(" | mismatches="); number(loopMismatches);
    text(" | pretest_discarded="); number(loopDiscarded);
    text(" | "); text(loopReason); text("\n");
  }
  flushOutput();
  if (outputPosition == outputLength) { loopPhase = LoopPhase::Idle; lastReportAt = millis(); }
}
}

namespace GpsReceiver {
bool hasFreshFix() { return !suspended && liveFix(); }
bool busy() {
  return backupTestRunning() || startupPending ||
#if TACTICPOD_REUSABLE
         profileRequested || profileRunning ||
#endif
         GpsProfile::active() || GpsConfig::active() || GpsRate::active() ||
         GpsUartScan::active() || GpsUartScan::faulted() || rawPhase != RawPhase::Idle ||
         loopPhase != LoopPhase::Idle;
}
void suspend() {
  if (suspended) return;
#if TACTICPOD_GPS_BACKUP_STANDBY
  {
    // Unacknowledged request; failing to enter only costs power, so never block on it.
    uint8_t frame[GpsPower::kFrameSize];
    GpsPower::buildBackupRequest(frame);
    if (GpsUartTx::write(frame, sizeof(frame)) == sizeof(frame)) {
      const uint32_t t0 = millis();
      while (!GpsUartTx::ready() && uint32_t(millis() - t0) < GpsPower::kTxDrainMs) delay(1);
      delay(5);
    }
  }
#endif
  Serial1.end();
  // Park our TX at the UART idle level instead of leaving the GPS RX input floating.
  pinMode(PIN_SERIAL1_TX, OUTPUT);
  digitalWrite(PIN_SERIAL1_TX, HIGH);
  suspended = true;
#if TACTICPOD_REUSABLE
  GpsProfile::invalidate("UNVERIFIED (UART suspended; receiver RAM not observed)");
#endif
}
void resume() {
  if (!suspended) return;
  Serial1.begin(GpsUartConfig::kNormalBaud);
  suspended = false;
#if TACTICPOD_GPS_BACKUP_STANDBY
  if (!backupTestRunning()) {
    GpsUartTx::resetIdle();
    wakeAttempts = 0; wakePending = true; sendWakeBytes();
  }
#endif
  fixReported = false;
  receivedByte = false;
#if TACTICPOD_REUSABLE
  receivedValid=false;lastRmcMs=0;slowRmc=0;
  startupPending=false;startupNoCommunication=false;
  profileRequested=true;profileRunning=profileFailed=false;
  profileRequestedAt=startedAt=millis();
  GpsProfile::invalidate("UNVERIFIED (UART wake; fresh RAM audit required)");
#endif
  if (startupNoCommunication) {
    startupNoCommunication = false; startupPending = true; receivedValid = false;
    startedAt = millis();
  }
}
void begin() {
  GpsProfile::invalidate("UNVERIFIED (receiver initialization; awaiting RAM readback)");
  Serial1.begin(GpsUartConfig::kNormalBaud);  // Verified pins; 38400 8N1, no PPS.
  startedAt = lastReportAt = millis();
  printing = false;
  startupPending = true; startupNoCommunication = false;
}
#if TACTICPOD_GPS_BACKUP_TEST
static GpsPower::BackupTest::Guards backupGuards() {
  GpsPower::BackupTest::Guards g;
  g.recording = Tcb3Session::recording();
  g.sessionBusy = Tcb3Session::exclusive();
  g.flashBusy = CompactRecorder::capturing() || RamObservations::capturing() || RamObservations::fastImu() ||
                Recorder::exclusive() || Recorder::exporting() || Tcb3Session::exporting();
  g.faulted = Tcb3Session::faulted();
  g.gpsBusy = busy() || GpsProfile::active();
  g.suspended = suspended;
  g.uartTxBusy = !uartTxReady();
  g.profileVerified = recordingReady();
  g.nmeaFresh = receivedValid && uint32_t(millis() - lastValidAt) <= kFreshMs;
  return g;
}
static void backupReport() {
  backupReported = true;
  using R = GpsPower::BackupTest::Result;
  const GpsPower::BackupTest& t = backupTest;
  const R r = t.result();
  backupLatched = t.blocksRecording();
  if (!Serial) return;
  Serial.print("GPS BACKUP TEST RESULT (wake mask 0x"); Serial.print(unsigned(TACTICPOD_GPS_WAKE_MASK), HEX); Serial.println(")");
  Serial.print("  standby request sent: "); Serial.println(t.requestOk() ? "YES" : "NO");
  Serial.print("  standby confirmed (UART silent "); Serial.print(unsigned(GpsPower::BackupTest::kListenMs));
  Serial.print(" ms after "); Serial.print(unsigned(GpsPower::BackupTest::kDwellMs)); Serial.print(" ms parked): ");
  Serial.println(!t.requestOk() ? "NOT TESTED" : t.standbySilent() ? "YES" : r == R::Aborted ? "NOT TESTED" : "NO (NMEA kept flowing; receiver did not enter standby)");
  Serial.print("  UART wake attempted: ");
  if (t.wakeAttempts()) { Serial.print("YES ("); Serial.print(t.wakeAttempts()); Serial.println(" attempts of 0xFF bytes)"); }
  else Serial.println("NO");
  Serial.print("  time until valid NMEA: ");
  if (t.wakeSucceeded()) { Serial.print(unsigned(t.validAfter())); Serial.println(" ms from first wake attempt"); }
  else Serial.println("NOT RETURNED");
  Serial.print("  GPS baud 38400: ");
  if (t.baudConfirmed()) Serial.println("CONFIRMED (checksum-valid NMEA)");
  else if (t.strayBytes() && r != R::EntryNotConfirmed) Serial.println("NOT CONFIRMED (bytes seen but no valid NMEA; receiver may have restarted at another baud)");
  else Serial.println("NOT CONFIRMED");
  Serial.print("  10 Hz configuration: ");
  Serial.println(t.profileVerified() ? "VERIFIED" : t.profileWasChecked() ? "FAILED" : "NOT CHECKED");
  Serial.print("  GPS fix: ");
  Serial.println(t.fixSeen() ? "YES" : t.profileVerified() ? "NO (not within 10 s; depends on sky view)" : "NOT CHECKED");
  switch (r) {
    case R::Pass: Serial.println("GPS BACKUP TEST: PASS"); break;
    case R::RequestNotSent: Serial.println("GPS BACKUP TEST: FAIL - request not sent (UART busy); GPS untouched."); break;
    case R::EntryNotConfirmed: Serial.println("GPS BACKUP TEST: FAIL - standby not confirmed; receiver kept running (baseline power). Wake was not tested."); break;
    case R::NoWake: Serial.println("GPS BACKUP TEST: FAIL - GPS did not wake. Recording is BLOCKED. A software reset cannot recover a receiver in standby: remove USB and battery to power-cycle it."); break;
    case R::NoValidNmea: Serial.println("GPS BACKUP TEST: FAIL - bytes returned but no valid NMEA at 38400. Recording is BLOCKED. Power-cycle (remove USB and battery) to restore the receiver."); break;
    case R::ProfileFailed: Serial.println("GPS BACKUP TEST: FAIL - NMEA returned but the 10 Hz configuration is not verified. Recording is BLOCKED until it verifies (GPS CONFIG / GPS RESTORE) or power is cycled."); break;
    default: Serial.println("GPS BACKUP TEST: ABORTED - recording/flash activity started; UART restored."); break;
  }
  Serial.println("GPS BACKUP TEST END");
}
static void backupStep() {
  if (backupLatched && receivedValid && GpsProfile::currentVerified()) backupLatched = false;
  if (!backupTest.running()) {
    if (backupTest.done() && !backupReported) backupReport();
    return;
  }
  GpsPower::BackupTest::Obs o;
  o.now = millis(); o.bytes = byteCount; o.txReady = uartTxReady();
  o.validNmea = receivedValid;
  o.profileVerified = recordingReady();
  o.profileFailed = profileFailed;
  o.fix = liveFix();
  const GpsPower::BackupTest::Guards g = backupGuards();
  o.guardViolated = g.recording || g.sessionBusy || g.flashBusy || g.faulted;
  switch (backupTest.step(o)) {
    case GpsPower::BackupTest::Action::SendRequest: {
      uint8_t frame[GpsPower::kFrameSize];
      GpsPower::buildBackupRequest(frame);
      backupTest.requestSent(GpsUartTx::write(frame, sizeof(frame)) == sizeof(frame), millis());
      break;
    }
    case GpsPower::BackupTest::Action::ParkUart:
      Serial1.end();
      pinMode(PIN_SERIAL1_TX, OUTPUT);
      digitalWrite(PIN_SERIAL1_TX, HIGH);
      suspended = true;
      GpsProfile::invalidate("UNVERIFIED (GPS BACKUP TEST; receiver RAM not observed)");
      break;
    case GpsPower::BackupTest::Action::ResumeUart:
      resume();
      GpsUartTx::resetIdle();
      break;
    case GpsPower::BackupTest::Action::SendWake:
      writeWakeBytes();
      break;
    default: break;
  }
  if (backupTest.done()) backupReport();
}
static void backupStart() {
  const GpsPower::BackupTest::Guards g = backupGuards();
  const char* why = GpsPower::BackupTest::refusal(g);
  if (backupTest.running()) why = "test already running";
  if (!why && !Serial) why = "USB serial is not open";
  if (why) { Serial.print("GPS BACKUP TEST refused: "); Serial.println(why); return; }
  backupTest.start(g, millis());
  backupReported = false;
  Serial.print("GPS BACKUP TEST started (about 20 s, USB stays awake; recording blocked). Wake mask 0x");
  Serial.println(unsigned(TACTICPOD_GPS_WAKE_MASK), HEX);
}
#endif
void update() {
#if TACTICPOD_GPS_BACKUP_TEST
  backupStep();
#endif
  if (suspended) return;
  GpsUartScan::update();
  if (!GpsUartScan::ownsReceiver()) pollUart();
#if TACTICPOD_GPS_BACKUP_STANDBY
  if (wakePending) {
    if (receivedValid) wakePending = false;
    else if (GpsPower::wakeRetryDue(millis(), lastWakeAt, wakeAttempts, false)) sendWakeBytes();
    else if (wakeAttempts >= GpsPower::kWakeAttempts && uint32_t(millis() - lastWakeAt) >= GpsPower::kWakeIntervalMs) {
      wakePending = false;  // Existing 30 s profile timeout reports REFUSED; recovery needs a power cycle.
      if (Serial) Serial.println("GPS WAKE FAILED: no NMEA after wake attempts; remove USB and battery to power-cycle the receiver.");
    }
  }
#endif
#if TACTICPOD_REUSABLE
  const bool sensorCapture=CompactRecorder::capturing() || Recorder::exclusive() || Tcb3Session::exporting();
  if(!Serial && !sensorCapture && !startupPending && !profileRequested && !profileRunning &&
     !profileFailed && uint32_t(millis()-GpsProfile::checkedAt())>=15000) {
    profileRequested=true;profileRequestedAt=millis();
  }
  if(profileRunning && !GpsProfile::active()) {
    profileRunning=profileRequested=false;profileFailed=!GpsProfile::currentVerified();
  }
  if(profileRequested && !profileRunning && !sensorCapture && !startupPending) {
    if(!receivedValid && uint32_t(millis()-profileRequestedAt)>=30000) {
      profileRequested=false;profileFailed=true;
      GpsProfile::invalidate("UNVERIFIED (no receiver communication after wake/start)");
    } else if(receivedValid && uint32_t(millis()-lastValidAt)<=kFreshMs &&
              !GpsProfile::active() && !GpsConfig::active() && !GpsRate::active() &&
              !GpsUartScan::active() && rawPhase==RawPhase::Idle && loopPhase==LoopPhase::Idle &&
              GpsProfile::startCheck(true))profileRunning=true;
  }
#endif
  if (startupPending) {
    if (!receivedValid && uint32_t(millis() - startedAt) >= 30000) {
      startupPending = false; startupNoCommunication = true;
    } else if (receivedValid && uint32_t(millis() - lastValidAt) <= kFreshMs &&
               rawPhase == RawPhase::Idle && loopPhase == LoopPhase::Idle && !GpsRate::active() &&
               !GpsUartScan::active() && !GpsUartScan::faulted() && !GpsConfig::active() &&
               !GpsProfile::active() && GpsProfile::startStartup()) {
      startupPending = false;
    }
  }
  GpsConfig::update();
  GpsProfile::update();
  GpsRate::update();
  if (GpsUartScan::active() || GpsUartScan::faulted()) return;
  if (GpsRate::active()) return;  // Quiet reporting; UART/parser/recording continue.
  if (GpsConfig::active()) return;
  if (GpsProfile::active()) return;
  serviceLoopback();
  if (rawPhase == RawPhase::Capture && uint32_t(millis() - rawStartedAt) >= kRawDurationMs)
    rawPhase = RawPhase::Dump;
  if (!Serial || Recorder::exporting()) {
    outputLength = outputPosition = 0;
    rawPendingAdvance = 0;  // Unsent raw line will be regenerated, not lost.
    return;
  }
  if (rawPhase != RawPhase::Idle) {
    serviceRawOutput();
    return;
  }
  if (loopPhase != LoopPhase::Idle) {
    reportLoopback();
    return;
  }
  if (printing && outputPosition == outputLength && uint32_t(millis() - lastReportAt) >= 1000) buildStatus();
  flushOutput();
}
bool prepareRecording() {
#if TACTICPOD_REUSABLE
  if(backupTestRunning() || suspended || GpsConfig::active() || GpsRate::active() || GpsUartScan::active() ||
     GpsUartScan::faulted() || rawPhase!=RawPhase::Idle || loopPhase!=LoopPhase::Idle ||
     (GpsProfile::active() && !GpsProfile::automaticStartup()))return false;
  profileFailed=false;
  if(!profileRequested && !profileRunning) {
    profileRequested=true;profileRequestedAt=millis();
    if(GpsProfile::active())profileRunning=true;
  }
  return true;
#else
  return true;
#endif
}
bool recordingReady() {
#if TACTICPOD_REUSABLE
  return !startupPending && !profileRequested && !profileRunning && GpsProfile::currentVerified()
#if TACTICPOD_GPS_BACKUP_TEST
      && !backupLatched && !backupTestRunning()
#endif
      ;
#else
  return true;
#endif
}
bool preparationFailed() {
#if TACTICPOD_REUSABLE
  return profileFailed || startupNoCommunication
#if TACTICPOD_GPS_BACKUP_TEST
      || backupLatched
#endif
      ;
#else
  return false;
#endif
}
bool handleCommand(const char* line) {
#if TACTICPOD_GPS_BACKUP_TEST
  if (strcmp(line, "GPS BACKUP TEST") == 0) { backupStart(); return true; }
  if (backupTest.running() && (strncmp(line, "GPS ", 4) == 0 || strcmp(line, "UART LOOPBACK TEST") == 0)) {
    Serial.println("GPS BACKUP TEST running; wait for GPS BACKUP TEST END before another GPS/UART command.");
    return true;
  }
#endif
  if (strcmp(line, "GPS 10HZ TEST") == 0 || strcmp(line, "GPS RESTORE") == 0) {
    const bool test = strcmp(line, "GPS 10HZ TEST") == 0;
    if (rawPhase != RawPhase::Idle || loopPhase != LoopPhase::Idle || GpsRate::active() ||
        GpsUartScan::active() || GpsUartScan::faulted() || GpsConfig::active() ||
        !(test ? GpsProfile::startTest() : GpsProfile::startRestore()))
      Serial.println("GPS profile command refused: recording/flash/UART busy, or snapshot unavailable/already retained. Stop recording; restore a saved test before another test.");
    else {
      startupPending = false; // Explicit TEST/RESTORE takes precedence for this boot.
      outputLength = outputPosition = 0;
    }
    return true;
  }
  if (GpsProfile::active() && (strcmp(line, "GPS CONFIG") == 0 || strcmp(line, "GPS RATE") == 0 ||
      strcmp(line, "GPS SCAN") == 0 || strcmp(line, "GPS RAW") == 0 || strcmp(line, "GPS STATUS") == 0 ||
      strcmp(line, "UART LOOPBACK TEST") == 0)) {
    Serial.println("GPS profile operation active; wait for GPS PROFILE END before another UART diagnostic.");
    return true;
  }
  if (strcmp(line, "GPS CONFIG") == 0) {
    if (rawPhase != RawPhase::Idle || loopPhase != LoopPhase::Idle || GpsRate::active() ||
        GpsUartScan::active() || GpsUartScan::faulted() || !GpsConfig::start())
      Serial.println("GPS CONFIG refused: wait for recording/flash/UART diagnostics to be stopped and idle.");
    else outputLength = outputPosition = 0;
    return true;
  }
  if (GpsConfig::active() && (strcmp(line, "GPS RATE") == 0 || strcmp(line, "GPS SCAN") == 0 ||
      strcmp(line, "GPS RAW") == 0 || strcmp(line, "GPS STATUS") == 0 || strcmp(line, "UART LOOPBACK TEST") == 0)) {
    Serial.println("GPS CONFIG active; wait for GPS CONFIG END before another UART diagnostic.");
    return true;
  }
  if (strcmp(line, "GPS RATE") == 0) {
    if (rawPhase != RawPhase::Idle || loopPhase != LoopPhase::Idle || GpsUartScan::active() || GpsUartScan::faulted() || !GpsRate::start())
      Serial.println("GPS RATE refused: rate/UART diagnostic or CSV export active; wait for its report.");
    else outputLength = outputPosition = 0;
    return true;
  }
  if (GpsRate::active() && (strcmp(line, "GPS SCAN") == 0 || strcmp(line, "GPS RAW") == 0 ||
      strcmp(line, "UART LOOPBACK TEST") == 0 || strcmp(line, "GPS STATUS") == 0)) {
    Serial.println("GPS RATE active; wait for GPS RATE END before another UART diagnostic.");
    return true;
  }
  if (strcmp(line, "GPS SCAN") == 0) {
    if (rawPhase != RawPhase::Idle || loopPhase != LoopPhase::Idle || !uartTxReady() || !GpsUartScan::start())
      Serial.println("GPS SCAN refused: recording/flash/UART busy, scanning or faulted; use RECORD STATUS and wait for STOPPED.");
    else {
      outputLength = outputPosition = 0;
      // Scan only starts with TX complete; its existing semaphore stays free
      // even if disabling UARTE resets the DMA AMOUNT register.
      GpsUartTx::resetIdle();
    }
    return true;
  }
  if ((GpsUartScan::active() || GpsUartScan::faulted()) &&
      (strcmp(line, "GPS RAW") == 0 || strcmp(line, "GPS STATUS") == 0 || strcmp(line, "UART LOOPBACK TEST") == 0)) {
    Serial.println("GPS UART scan active or restoration faulted; wait for GPS SCAN END (or reset if requested).");
    return true;
  }
  if (strcmp(line, "UART LOOPBACK TEST") == 0) {
    if (loopPhase != LoopPhase::Idle || rawPhase != RawPhase::Idle)
      Serial.println("UART LOOPBACK TEST refused: another UART diagnostic is active.");
    else if (Recorder::exporting()) Serial.println("UART LOOPBACK TEST refused during CSV export.");
    else {
      loopSent = loopReceived = loopMismatches = loopDiscarded = 0;
      loopStartedAt = millis(); loopPhase = LoopPhase::Quiet;
      outputLength = outputPosition = 0;
      text("UART LOOPBACK TEST: disconnect GPS TX/RX, bridge XIAO D6 TX to D7 RX. Testing Serial1 at unchanged 38400 baud.\n");
    }
    return true;
  }
  if (loopPhase != LoopPhase::Idle &&
      (strcmp(line, "GPS RAW") == 0 || strcmp(line, "GPS STATUS") == 0)) {
    Serial.println("UART loopback active; wait for its PASS/FAIL report before GPS RAW/STATUS.");
    return true;
  }
  if (strcmp(line, "GPS RAW") == 0) {
    if (rawPhase != RawPhase::Idle) Serial.println("GPS RAW already active or awaiting output; capture not restarted.");
    else if (Recorder::exporting()) Serial.println("GPS RAW refused during CSV export; UART reception continues.");
    else {
      rawTotal = rawValidSentences = 0;
      rawStored = rawCursor = rawPendingAdvance = 0;
      outputLength = outputPosition = 0;
      rawValidator = TinyGPSPlus();
      rawStartedAt = millis(); rawPhase = RawPhase::Capture;
      text("GPS RAW: capturing UART at 38400 baud for 10 seconds in RAM; escaped data and summary follow. GPS configuration unchanged.\n");
    }
    return true;
  }
  if (strcmp(line, "GPS ON") == 0) {
    printing = true; lastReportAt = millis();
    Serial.println("GPS printing ON (once per second); UART reception stays at 38400 baud.");
    return true;
  }
  if (strcmp(line, "GPS OFF") == 0) {
    printing = false;
    if (rawPhase == RawPhase::Idle && loopPhase == LoopPhase::Idle) outputLength = outputPosition = 0;
    Serial.println("GPS printing OFF; UART reception continues.");
    return true;
  }
  if (strcmp(line, "GPS STATUS") == 0) {
    if (rawPhase != RawPhase::Idle) Serial.println("GPS RAW is active; wait for GPS RAW END before requesting GPS STATUS.");
    else if (Recorder::exporting()) Serial.println("GPS STATUS refused during CSV export; UART reception continues.");
    else buildStatus();
    return true;
  }
  return false;
}
// A completed UART fault must not lock the independent IMU/flash recorder.
bool uartScanBusy() { return GpsUartScan::active() || GpsConfig::active() || GpsProfile::active(); }
}
