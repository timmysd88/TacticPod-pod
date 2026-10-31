#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <string.h>
#include "gps_rate.h"
#include "gps_uart_config.h"
#include "recorder.h"

namespace {
namespace Rate {
constexpr uint32_t kWindowMs = 15000;
constexpr uint32_t kDayMs = 86400000UL;
enum class Phase { Idle, Measure, Report };
Phase phase = Phase::Idle;
struct Family {
  uint32_t sentences = 0, wireBytes = 0, fixes = 0, noFix = 0, unknownFix = 0;
  uint32_t epochs = 0, duplicates = 0, missingUtc = 0, backwards = 0, lastUtc = 0;
  bool hasUtc = false;
};
Family gga, rmc;
uint32_t started = 0, bytes = 0, validNmeaCount = 0, badChecksum = 0, overlong = 0;
char frame[192], line[256];
size_t frameLength = 0, lineLength = 0;
bool collecting = false, sawCr = false, notice = false;
unsigned reportLine = 0;

void text(const char* p) {
  while (*p && lineLength + 1 < sizeof(line)) line[lineLength++] = *p++;
  line[lineLength] = 0;
}
void number(uint32_t n) {
  char digits[11]; char* p = digits + 10; *p = 0;
  do { *--p = '0' + n % 10; n /= 10; } while (n);
  text(p);
}
void ratio(uint64_t numerator, uint64_t denominator, unsigned places = 3) {
  if (!denominator) { text("N/A"); return; }
  uint32_t scale = 1;
  for (unsigned i = 0; i < places; ++i) scale *= 10;
  const uint32_t scaled = (numerator * scale + denominator / 2) / denominator;
  number(scaled / scale);
  if (places) text(".");
  for (uint32_t divisor = scale / 10; divisor; divisor /= 10) {
    char digit[2] = {char('0' + (scaled / divisor) % 10), 0}; text(digit);
  }
}
struct Field { const char* p; size_t size; };
Field field(unsigned index) {
  const char* p = frame;
  while (index--) { p = strchr(p, ','); if (!p) return {nullptr, 0}; ++p; }
  const char* end = strchr(p, ',');
  return {p, end ? size_t(end - p) : strlen(p)};
}
int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
bool utc(Field f, uint32_t& result) {
  if (!f.p || f.size < 6) return false;
  for (unsigned i = 0; i < 6; ++i) if (f.p[i] < '0' || f.p[i] > '9') return false;
  const unsigned hour = (f.p[0] - '0') * 10 + f.p[1] - '0';
  const unsigned minute = (f.p[2] - '0') * 10 + f.p[3] - '0';
  const unsigned second = (f.p[4] - '0') * 10 + f.p[5] - '0';
  if (hour > 23 || minute > 59 || second > 59) return false;
  uint32_t fraction = 0, multiplier = 100;
  if (f.size > 6) {
    if (f.p[6] != '.' || f.size == 7) return false;
    for (size_t i = 7; i < f.size; ++i) {
      if (f.p[i] < '0' || f.p[i] > '9') return false;
      fraction += (f.p[i] - '0') * multiplier; multiplier /= 10;
    }
  }
  result = ((hour * 60 + minute) * 60 + second) * 1000 + fraction;
  return true;
}
void finishFrame() {
  // Count complete CRLF-terminated, checksum-valid sentences, not location
  // commits (TinyGPSPlus intentionally skips location commits without a fix).
  if (frameLength < 10 || frame[frameLength - 3] != '*') return;
  const int high = hexDigit(frame[frameLength - 2]), low = hexDigit(frame[frameLength - 1]);
  if (high < 0 || low < 0) return;
  uint8_t checksum = 0;
  for (size_t i = 1; i < frameLength - 3; ++i) checksum ^= uint8_t(frame[i]);
  if (checksum != (high * 16 + low)) { ++badChecksum; return; }
  ++validNmeaCount;
  if (frame[1] < 'A' || frame[1] > 'Z' || frame[2] < 'A' || frame[2] > 'Z' || frame[6] != ',') return;
  const bool isGga = memcmp(frame + 3, "GGA", 3) == 0;
  const bool isRmc = memcmp(frame + 3, "RMC", 3) == 0;
  if (!isGga && !isRmc) return;
  Family& f = isGga ? gga : rmc;
  ++f.sentences; f.wireBytes += frameLength + 2;
  frame[frameLength - 3] = 0;
  const Field fix = field(isGga ? 6 : 2);
  if (fix.size == 1 && (isGga ? fix.p[0] >= '1' && fix.p[0] <= '5' : fix.p[0] == 'A')) ++f.fixes;
  else if (fix.size == 1 && fix.p[0] == (isGga ? '0' : 'V')) ++f.noFix;
  else ++f.unknownFix;
  uint32_t time = 0;
  if (!utc(field(1), time)) { ++f.missingUtc; return; }
  if (!f.hasUtc) { f.hasUtc = true; f.lastUtc = time; ++f.epochs; return; }
  const uint32_t delta = (time + kDayMs - f.lastUtc) % kDayMs;
  if (delta == 0) ++f.duplicates;
  else if (delta < kDayMs / 2) { ++f.epochs; f.lastUtc = time; }
  else ++f.backwards;
}
const Family& primary() {
  // RMC is emitted every navigation epoch; GGA may deliberately run at 1Hz.
  // Missing RMC UTC must remain UNKNOWN, not select another family's cadence.
  return rmc.sentences ? rmc : gga;
}
void familyLine(const Family& f, const char* name, bool epochs) {
  text("GPS RATE "); text(name);
  if (!epochs) {
    text(" | sentences="); number(f.sentences);
    text(" | sentence_hz="); ratio(uint64_t(f.sentences) * 1000, kWindowMs);
    text(" | valid_fix="); number(f.fixes); text(" | no_fix="); number(f.noFix);
    text(" | unknown_fix="); number(f.unknownFix); text(" | mean_wire_bytes="); ratio(f.wireBytes, f.sentences, 1);
  } else {
    text(" | utc_epochs="); number(f.epochs);
    text(" | utc_epoch_hz="); ratio(uint64_t(f.epochs) * 1000, kWindowMs);
    text(" | duplicate_utc="); number(f.duplicates); text(" | missing_utc="); number(f.missingUtc);
    text(" | backwards_utc="); number(f.backwards);
  }
  if (&f == &gga) text(" | role=SUPPORTING");
}
void renderLine() {
  const Family& p = primary();
  if (reportLine == 0) {
    text("GPS RATE RESULT | window_ms="); number(kWindowMs);
    text(" | UART="); number(GpsUartConfig::kNormalBaud);
    text(" | bytes="); number(bytes); text(" | checksum_valid="); number(validNmeaCount);
    text(" | checksum_failed="); number(badChecksum); text(" | overlong_frames="); number(overlong);
  } else if (reportLine == 1) familyLine(gga, "GGA", false);
  else if (reportLine == 2) familyLine(gga, "GGA", true);
  else if (reportLine == 3) familyLine(rmc, "RMC", false);
  else if (reportLine == 4) familyLine(rmc, "RMC", true);
  else if (reportLine == 5) {
    text("GPS RATE BANDWIDTH | current_Bps="); ratio(uint64_t(bytes) * 1000, kWindowMs, 1);
    text(" | UART_capacity_Bps="); number(GpsUartConfig::kNormalBaud / 10);
    text(" | GGA_RMC_only_at10Hz_Bps=");
    ratio(10 * (uint64_t(gga.wireBytes) * rmc.sentences + uint64_t(rmc.wireBytes) * gga.sentences), uint64_t(gga.sentences) * rmc.sentences, 1);
    text(" | all_stream_scaled_to10Hz_Bps="); ratio(uint64_t(bytes) * 10, p.epochs >= 2 ? p.epochs : 0, 1);
  } else if (reportLine == 6) {
    text("GPS RATE OBSERVED | primary="); text(p.sentences ? (&p == &gga ? "GGA" : "RMC") : "NONE");
    text(" | position_sentence_hz="); ratio(uint64_t(p.sentences) * 1000, kWindowMs);
    text(" | utc_epoch_hz="); ratio(uint64_t(p.epochs) * 1000, kWindowMs);
    text(" | output_cadence_10hz=");
    if (p.epochs < 2) text("UNKNOWN (insufficient advancing UTC)");
    // Keep the existing roughly 5% tolerance: a window can lose a sentence
    // crossing either boundary, so 149/15s (9.933Hz) still qualifies.
    else text(p.epochs >= 143 && p.epochs <= 157 ? "YES (approximately)" : "NO");
  } else {
    text("GPS RATE END | fixes="); text(gga.fixes + rmc.fixes ? "PRESENT" : "NONE_OBSERVED");
    text(" | GGA/RMC epochs are NOT summed; output cadence does not prove valid 10 Hz navigation solutions.\n");
    return;
  }
  text("\n");
}
}
}

namespace GpsRate {
bool active() { return Rate::phase != Rate::Phase::Idle; }
bool start() {
  using namespace Rate;
  if (active() || Recorder::exporting()) return false;
  gga = Family{}; rmc = Family{};
  bytes = validNmeaCount = badChecksum = overlong = 0;
  frameLength = lineLength = 0; collecting = sawCr = false;
  started = millis(); notice = true; reportLine = 0; phase = Phase::Measure;
  return true;
}
void observe(uint8_t value) {
  using namespace Rate;
  if (phase != Phase::Measure || uint32_t(millis() - started) >= kWindowMs) return;
  ++bytes;
  if (value == '$') { frameLength = 1; frame[0] = '$'; collecting = true; sawCr = false; return; }
  if (!collecting) return;
  if (value == '\n') {
    if (sawCr) { frame[frameLength] = 0; finishFrame(); }
    collecting = false; return;
  }
  if (value == '\r') { sawCr = true; return; }
  if (sawCr || value < 0x20 || value > 0x7E) { collecting = false; return; }
  if (frameLength >= sizeof(frame) - 1) { ++overlong; collecting = false; return; }
  frame[frameLength++] = char(value);
}
void update() {
  using namespace Rate;
  if (phase == Phase::Idle) return;
  if (phase == Phase::Measure && uint32_t(millis() - started) >= kWindowMs) {
    phase = Phase::Report; notice = false; lineLength = 0;
  }
  if (!Serial || Recorder::exporting()) return;
  if (phase == Phase::Measure) {
    const char message[] = "GPS RATE: observing checksum-valid GGA and RMC for 15000 ms at 38400 baud; no fix required. Receiver settings unchanged.\n";
    if (notice && Serial.availableForWrite() >= int(sizeof(message) - 1)) {
      Serial.write(reinterpret_cast<const uint8_t*>(message), sizeof(message) - 1); notice = false;
    }
    return;
  }
  if (!lineLength) renderLine();
  if (Serial.availableForWrite() < int(lineLength)) return;
  if (Serial.write(reinterpret_cast<const uint8_t*>(line), lineLength) != lineLength) return;
  lineLength = 0;
  if (++reportLine == 8) phase = Phase::Idle;
}
}
