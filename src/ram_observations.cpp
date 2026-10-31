#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <string.h>
#include "ram_observations.h"
#include "monotonic_clock.h"
#include "recorder.h"
#include "compact_recorder.h"
#include "tcb3_session.h"

namespace {
namespace ObsDetail {
using namespace RamObservations;
uint64_t le(const uint8_t* p, unsigned n) {
  uint64_t v = 0; for (unsigned i = 0; i < n; ++i) v |= uint64_t(p[i]) << (8 * i); return v;
}
void put(uint8_t* p, uint64_t v, unsigned n) { for (unsigned i = 0; i < n; ++i) p[i] = v >> (8 * i); }
void record(uint8_t* p, unsigned type, unsigned size, uint64_t at) {
  memset(p, 0, size); p[0] = type; p[1] = 1; put(p + 2, size, 2); put(p + 4, at, 8);
}
bool captureEnabled = true;
template<class T, unsigned N> struct Queue {
  T items[N]; unsigned head = 0; Stats counts = {};
  void clear() { head = 0; counts = Stats{}; counts.capacity = N; }
  void push(const T& value) {
    Tcb3Session::observation(value.bytes[0]);
#ifdef TCB_SIM_OBSERVATION
    TCB_SIM_OBSERVATION(value.bytes,sizeof(value));
#endif
    CompactRecorder::observe(value.bytes, sizeof(value));
    if (!captureEnabled) return;
    const uint64_t at = le(value.bytes + 4, 8);
    if (counts.observed) {
      const uint64_t spacing = at - counts.lastUs;
      if (counts.observed == 1 || spacing < counts.minSpacingUs) counts.minSpacingUs = spacing;
      if (spacing > counts.maxSpacingUs) counts.maxSpacingUs = spacing;
    } else counts.firstUs = at;
    counts.lastUs = at; ++counts.observed;
    if (counts.used == N) { ++counts.dropped; return; }
    items[(head + counts.used++) % N] = value; ++counts.accepted;
    if (counts.used > counts.highWater) counts.highWater = counts.used;
  }
  bool pop(T& value) {
    if (!counts.used) return false;
    value = items[head]; head = (head + 1) % N; --counts.used; return true;
  }
};
Queue<Imu, 256> imuQueue;
Queue<Rmc, 32> rmcQueue;
Queue<Gga, 8> ggaQueue;
Queue<Event, 16> eventQueue;
bool initialized = false, fast = false;
ParserStats parser = {};
char frame[192]; unsigned frameSize = 0;
bool collecting = false, carriage = false;
uint64_t frameAt = 0, anchorAt = 0;
uint32_t anchorDate = 0, anchorUtc = 0;
bool anchorValid = false;
char* fields[20]; unsigned fieldCount = 0;
const char* field(unsigned i) { return i < fieldCount ? fields[i] : ""; }
int digit(char c) { return c >= '0' && c <= '9' ? c - '0' : -1; }
int hexDigit(char c) { return digit(c) >= 0 ? digit(c) : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
bool one(const char* s, char c) { return s[0] == c && !s[1]; }
// Fixed-point decimal conversion, bounded integer arithmetic, nearest rounding.
bool decimal(const char* s, uint64_t scale, uint64_t limit, uint64_t& value) {
  if (digit(*s) < 0) return false;
  uint64_t whole = 0, fraction = 0, denominator = 1;
  while (digit(*s) >= 0) {
    const unsigned d = digit(*s++);
    if (whole > limit / 10 || whole * 10 + d > limit) return false;
    whole = whole * 10 + d;
  }
  if (*s == '.') {
    ++s; if (digit(*s) < 0) return false;
    while (digit(*s) >= 0) {
      if (denominator == 1000000000ULL) return false;
      fraction = fraction * 10 + digit(*s++); denominator *= 10;
    }
  }
  if (*s || whole > limit / scale) return false;
  value = whole * scale + (fraction * scale + denominator / 2) / denominator;
  return value <= limit;
}
bool signedDecimal(const char* s, int32_t& value) {
  const bool negative = *s == '-'; if (negative || *s == '+') ++s;
  uint64_t magnitude;
  if (!decimal(s, 1000, negative ? 2147483648ULL : 2147483647ULL, magnitude)) return false;
  value = negative ? int32_t(-int64_t(magnitude)) : int32_t(magnitude); return true;
}
bool coordinate(const char* s, const char* hemisphere, bool latitude, int32_t& result) {
  const unsigned degreeDigits = latitude ? 2 : 3;
  if (strlen(s) < degreeDigits + 2 || strlen(hemisphere) != 1) return false;
  unsigned degrees = 0;
  for (unsigned i = 0; i < degreeDigits; ++i) {
    if (digit(s[i]) < 0) return false;
    degrees = degrees * 10 + digit(s[i]);
  }
  if (digit(s[degreeDigits]) < 0 || digit(s[degreeDigits + 1]) < 0 ||
      (s[degreeDigits + 2] && s[degreeDigits + 2] != '.')) return false;
  uint64_t minutes;
  if (!decimal(s + degreeDigits, 1000000000ULL, 59999999999ULL, minutes)) return false;
  const unsigned bound = latitude ? 90 : 180;
  if (degrees > bound || (degrees == bound && minutes)) return false;
  const char positive = latitude ? 'N' : 'E', negative = latitude ? 'S' : 'W';
  if (*hemisphere != positive && *hemisphere != negative) return false;
  result = int32_t(uint64_t(degrees) * 10000000 + (minutes + 3000) / 6000);
  if (*hemisphere == negative) result = -result;
  return true;
}
bool leapYear(unsigned y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
unsigned monthDays(unsigned y, unsigned m) {
  const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  return m >= 1 && m <= 12 ? days[m - 1] + (m == 2 && leapYear(y) ? 1 : 0) : 0;
}
bool date(const char* s, uint32_t& out) {
  if (strlen(s) != 6) return false;
  for (unsigned i = 0; i < 6; ++i) if (digit(s[i]) < 0) return false;
  const unsigned day = digit(s[0]) * 10 + digit(s[1]), month = digit(s[2]) * 10 + digit(s[3]);
  const unsigned year = 2000 + digit(s[4]) * 10 + digit(s[5]);
  if (!day || day > monthDays(year, month)) return false;
  out = year * 10000 + month * 100 + day; return true;
}
bool utc(const char* s, uint32_t& out, bool& leap) {
  leap = false;
  if (strlen(s) < 6) return false;
  for (unsigned i = 0; i < 6; ++i) if (digit(s[i]) < 0) return false;
  const unsigned h = digit(s[0]) * 10 + digit(s[1]), m = digit(s[2]) * 10 + digit(s[3]);
  const unsigned sec = digit(s[4]) * 10 + digit(s[5]);
  if (h > 23 || m > 59 || sec > 60) return false;
  uint32_t fraction = 0, multiplier = 100; unsigned digits = 0;
  s += 6;
  if (*s == '.') {
    ++s; if (digit(*s) < 0) return false;
    while (digit(*s) >= 0) {
      if (++digits > 9) return false;
      fraction += digit(*s++) * multiplier; multiplier /= 10;
    }
  }
  if (*s) return false;
  if (sec == 60) { leap = true; return false; }
  out = (h * 3600 + m * 60 + sec) * 1000 + fraction; return true;
}
uint32_t adjacentDate(uint32_t d, int direction) {
  unsigned y = d / 10000, m = d / 100 % 100, day = d % 100;
  if (direction > 0) {
    if (++day > monthDays(y, m)) { day = 1; if (++m > 12) { m = 1; ++y; } }
  } else if (--day == 0) { if (--m == 0) { m = 12; --y; } day = monthDays(y, m); }
  return y >= 2000 && y <= 2099 ? y * 10000 + m * 100 + day : 0;
}
void leapEvent(uint64_t at, unsigned stream) {
  ++parser.leapSeconds;
  Event value; record(value.bytes, 4, sizeof(value), at);
  put(value.bytes + 12, 5, 2); value.bytes[14] = 1; value.bytes[15] = stream;
  put(value.bytes + 16, 2, 4); put(value.bytes + 20, 1, 4);
  put(value.bytes + 24, anchorValid ? anchorUtc : UINT64_MAX, 8); eventQueue.push(value);
}
void invalid(const char* s, bool valid) { if (*s && !valid) ++parser.malformedFields; }
void complete() {
  if (frameSize < 10 || frame[frameSize - 3] != '*') { ++parser.partial; return; }
  const int a = hexDigit(frame[frameSize - 2]), b = hexDigit(frame[frameSize - 1]);
  if (a < 0 || b < 0) { ++parser.partial; return; }
  uint8_t checksum = 0;
  for (unsigned i = 1; i < frameSize - 3; ++i) checksum ^= uint8_t(frame[i]);
  if (checksum != ((a << 4) | b)) { ++parser.checksumFailures; return; }
  if (frame[1] < 'A' || frame[1] > 'Z' || frame[2] < 'A' || frame[2] > 'Z' || frame[6] != ',') return;
  const bool isRmc = memcmp(frame + 3, "RMC", 3) == 0, isGga = memcmp(frame + 3, "GGA", 3) == 0;
  if (!isRmc && !isGga) return;
  const uint64_t at = MonotonicClock::now();
  frame[frameSize - 3] = 0; fieldCount = 0; fields[fieldCount++] = frame + 1;
  for (char* p = frame + 1; *p; ++p) if (*p == ',') {
    *p = 0; if (fieldCount < 20) fields[fieldCount++] = p + 1;
  }
  uint32_t mask = 0, tod = 0, day = 0; bool leap = false;
  const bool hasTime = utc(field(1), tod, leap); invalid(field(1), hasTime);
  if (leap) leapEvent(at, isRmc ? 2 : 3);
  if (isRmc) {
    Rmc value; record(value.bytes, 2, sizeof(value), at);
    int32_t lat = 0, lon = 0;
    const bool latOk = coordinate(field(3), field(4), true, lat), lonOk = coordinate(field(5), field(6), false, lon);
    invalid(field(3), latOk); invalid(field(5), lonOk);
    if (latOk && lonOk) { mask |= 1; put(value.bytes + 16, uint32_t(lat), 4); put(value.bytes + 20, uint32_t(lon), 4); }
    const char* status = field(2), *mode = field(12);
    if (one(status, 'A') || one(status, 'V')) value.bytes[40] = *status;
    else invalid(status, false);
    if (mode[0] >= 'A' && mode[0] <= 'Z' && !mode[1]) { mask |= 64; value.bytes[41] = *mode; }
    else invalid(mode, false);
    if ((mask & 1) && value.bytes[40] == 'A' && value.bytes[41] != 'N') mask |= 2;
    uint64_t number = 0;
    bool ok = decimal(field(7), 1000000, uint64_t(UINT32_MAX) * 3600000 / 1852, number);
    if (ok) {
      const uint64_t speed = (number * 1852 + 1800000) / 3600000;
      if (speed <= UINT32_MAX) { mask |= 4; put(value.bytes + 24, speed, 4); } else ok = false;
    }
    invalid(field(7), ok);
    ok = decimal(field(8), 100000, 35999999, number);
    if (ok) { mask |= 8; put(value.bytes + 28, number, 4); } invalid(field(8), ok);
    ok = date(field(9), day);
    if (ok) { mask |= 16; put(value.bytes + 32, day, 4); } invalid(field(9), ok);
    if (hasTime) { mask |= 32; put(value.bytes + 36, tod, 4); }
    value.bytes[42] = frame[1]; value.bytes[43] = frame[2]; put(value.bytes + 12, mask, 4);
    rmcQueue.push(value);
    anchorValid = (mask & 48) == 48;
    if (anchorValid) { anchorDate = day; anchorUtc = tod; anchorAt = at; }
  } else {
    Gga value; record(value.bytes, 3, sizeof(value), at); value.bytes[36] = 255;
    if (hasTime) {
      mask |= 1; put(value.bytes + 20, tod, 4);
      if (anchorValid && at - anchorAt <= 1500000) {
        int64_t delta = int64_t(tod) - anchorUtc; int direction = 0;
        if (delta < -43200000) { delta += 86400000; direction = 1; }
        else if (delta > 43200000) { delta -= 86400000; direction = -1; }
        day = direction ? adjacentDate(anchorDate, direction) : anchorDate;
        if (day && delta >= -1500 && delta <= 1500) { mask |= 2; put(value.bytes + 16, day, 4); }
      }
    }
    uint64_t number = 0; int32_t signedNumber = 0;
    bool ok = decimal(field(6), 1, 254, number) && !strchr(field(6), '.');
    if (ok) value.bytes[36] = number;
    invalid(field(6), ok);
    ok = decimal(field(7), 1, 65535, number) && !strchr(field(7), '.');
    if (ok) { mask |= 32; put(value.bytes + 34, number, 2); } invalid(field(7), ok);
    ok = decimal(field(8), 100, 65535, number);
    if (ok) { mask |= 16; put(value.bytes + 32, number, 2); } invalid(field(8), ok);
    ok = signedDecimal(field(9), signedNumber) && one(field(10), 'M');
    if (ok) { mask |= 4; put(value.bytes + 24, uint32_t(signedNumber), 4); } invalid(field(9), ok);
    ok = signedDecimal(field(11), signedNumber) && one(field(12), 'M');
    if (ok) { mask |= 8; put(value.bytes + 28, uint32_t(signedNumber), 4); } invalid(field(11), ok);
    value.bytes[37] = frame[1]; value.bytes[38] = frame[2]; put(value.bytes + 12, mask, 4); ggaQueue.push(value);
  }
}
void clear() {
  imuQueue.clear(); rmcQueue.clear(); ggaQueue.clear(); eventQueue.clear(); parser = ParserStats{};
  collecting = carriage = anchorValid = false; frameSize = 0;
}
char line[256]; size_t lineLength = 0;
unsigned report = 0; bool reporting = false;
Stats snapshot[4]; ParserStats parserSnapshot;
const char* notice = "RAM only";
void text(const char* s) { while (*s && lineLength + 1 < sizeof(line)) line[lineLength++] = *s++; }
void number(uint64_t n) {
  char buffer[21]; char* p = buffer + 20; *p = 0;
  do { *--p = '0' + n % 10; n /= 10; } while (n); text(p);
}
void request(const char* reason) {
  notice = reason; report = 0; lineLength = 0; reporting = true;
  snapshot[0] = imuQueue.counts; snapshot[1] = rmcQueue.counts;
  snapshot[2] = ggaQueue.counts; snapshot[3] = eventQueue.counts; parserSnapshot = parser;
}
}
}

namespace RamObservations {
using namespace ObsDetail;
void begin() { if (!initialized) { initialized = true; clear(); } }
bool fastImu() { return (captureEnabled && fast) || CompactRecorder::capturing(); }
bool capturing() { return captureEnabled; }
void addImu(uint64_t at, const int16_t axes[6]) {
  begin(); if (!captureEnabled && !CompactRecorder::capturing()) return;
  Imu value; record(value.bytes, 1, sizeof(value), at);
  for (unsigned i = 0; i < 6; ++i) put(value.bytes + 12 + i * 2, uint16_t(axes[i]), 2);
  imuQueue.push(value);
}
void interruptFrame() { if (collecting) { collecting = false; ++parser.partial; } }
void observe(uint8_t value) {
  begin(); if (!captureEnabled && !CompactRecorder::capturing()) return;
  const uint64_t at = MonotonicClock::now();
  if (collecting && at - frameAt >= 1000000) interruptFrame();
  if (value == '$') { interruptFrame(); collecting = true; carriage = false; frameSize = 1; frame[0] = '$'; frameAt = at; return; }
  if (!collecting) return;
  if (value == '\n') { if (carriage) { frame[frameSize] = 0; complete(); } else ++parser.partial; collecting = false; return; }
  if (value == '\r' && !carriage) { carriage = true; return; }
  if (carriage || value < 32 || value > 126) { interruptFrame(); return; }
  if (frameSize == sizeof(frame) - 1) { ++parser.overlong; collecting = false; return; }
  frame[frameSize++] = char(value);
}
bool pop(Imu& value) { begin(); return imuQueue.pop(value); }
bool pop(Rmc& value) { begin(); return rmcQueue.pop(value); }
bool pop(Gga& value) { begin(); return ggaQueue.pop(value); }
bool pop(Event& value) { begin(); return eventQueue.pop(value); }
Stats stats(unsigned stream) {
  begin(); return stream == 1 ? imuQueue.counts : stream == 2 ? rmcQueue.counts : stream == 3 ? ggaQueue.counts : stream == 4 ? eventQueue.counts : Stats{};
}
ParserStats parserStats() { return parser; }
bool handleCommand(const char* command) {
  const bool start = strcmp(command, "OBS START") == 0, stop = strcmp(command, "OBS STOP") == 0;
  const bool reset = strcmp(command, "OBS CLEAR") == 0, status = strcmp(command, "OBS STATUS") == 0;
  if (!start && !stop && !reset && !status) return false;
  begin();
  if (Recorder::exporting()) { request("REFUSED during CSV export"); return true; }
  if (start || reset) clear();
  if (start) captureEnabled = fast = true;
  if (stop) {
    captureEnabled = fast = false;
    collecting = carriage = anchorValid = false; frameSize = 0;
  }
  request(start ? "START: RAM capture at 100Hz; flash unchanged" : stop ? "STOP: RAM capture stopped; V1 unchanged" : reset ? "CLEAR: RAM queues/stats cleared; clock/flash unchanged" : "RAM only");
  return true;
}
void update() {
  begin();
  if (collecting && MonotonicClock::now() - frameAt >= 1000000) interruptFrame();
  if (!reporting || !Serial || Recorder::exporting()) return;
  if (!lineLength) {
    if (!report) {
      text("OBS STATUS | "); text(notice); text(" | capture="); text(captureEnabled ? "ON" : "OFF");
      text(" | RAM_fast_imu="); text(fastImu() ? "100Hz" : "OFF");
    } else if (report <= 4) {
      const Stats& s = snapshot[report - 1];
      text(report == 1 ? "OBS IMU" : report == 2 ? "OBS RMC" : report == 3 ? "OBS GGA" : "OBS EVENT");
      text(" | seen="); number(s.observed); text(" accepted="); number(s.accepted); text(" overflow="); number(s.dropped);
      text(" | queue="); number(s.used); text("/"); number(s.capacity); text(" high="); number(s.highWater);
      text(" | spacing_us_min="); number(s.minSpacingUs); text(" max="); number(s.maxSpacingUs);
    } else if (report == 5) {
      text("OBS PARSER | checksum_fail="); number(parserSnapshot.checksumFailures);
      text(" malformed_fields="); number(parserSnapshot.malformedFields); text(" overlong="); number(parserSnapshot.overlong);
      text(" partial="); number(parserSnapshot.partial); text(" leap_seconds="); number(parserSnapshot.leapSeconds);
    } else { text("OBS END | first/last observation timestamps available through RAM API; no V2 flash writes"); }
    text("\n");
  }
  if (Serial.availableForWrite() < int(lineLength)) return;
  if (Serial.write(reinterpret_cast<const uint8_t*>(line), lineLength) != lineLength) return;
  lineLength = 0; if (++report == 7) reporting = false;
}
}
