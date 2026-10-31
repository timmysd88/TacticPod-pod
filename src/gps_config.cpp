#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <string.h>
#include "gps_config.h"
#include "gps_uart_tx.h"
#include "recorder.h"
#include "flash_write_test.h"
#include "gps_profile.h"

namespace {
namespace Config {
// u-blox M10 SPG 5.00 / 5.21 Interface descriptions, CFG-VALGET and
// configuration database tables. See docs/gps-config.md for primary sources.
enum class Decode { Boolean, Number, Milliseconds, NavRatio, TimeRef, Power,
                    Dynamic, FixMode, Rate, OtpClock };
struct Key { uint32_t id; const char* name; Decode decode; };
const Key keys[] = {
  {0x30210001, "CFG-RATE-MEAS", Decode::Milliseconds},
  {0x30210002, "CFG-RATE-NAV", Decode::NavRatio},
  {0x20210003, "CFG-RATE-TIMEREF", Decode::TimeRef},
  {0x1031001f, "CFG-SIGNAL-GPS_ENA", Decode::Boolean},
  {0x10310021, "CFG-SIGNAL-GAL_ENA", Decode::Boolean},
  {0x10310022, "CFG-SIGNAL-BDS_ENA", Decode::Boolean},
  {0x10310025, "CFG-SIGNAL-GLO_ENA", Decode::Boolean},
  {0x10310024, "CFG-SIGNAL-QZSS_ENA", Decode::Boolean},
  {0x10310020, "CFG-SIGNAL-SBAS_ENA", Decode::Boolean},
  {0x10310001, "CFG-SIGNAL-GPS_L1CA_ENA", Decode::Boolean},
  {0x10310007, "CFG-SIGNAL-GAL_E1_ENA", Decode::Boolean},
  {0x1031000d, "CFG-SIGNAL-BDS_B1_ENA", Decode::Boolean},
  {0x1031000f, "CFG-SIGNAL-BDS_B1C_ENA", Decode::Boolean},
  {0x10310018, "CFG-SIGNAL-GLO_L1_ENA", Decode::Boolean},
  {0x40520001, "CFG-UART1-BAUDRATE", Decode::Number},
  {0x10520005, "CFG-UART1-ENABLED", Decode::Boolean},
  {0x10730001, "CFG-UART1INPROT-UBX", Decode::Boolean},
  {0x10740001, "CFG-UART1OUTPROT-UBX", Decode::Boolean},
  {0x10740002, "CFG-UART1OUTPROT-NMEA", Decode::Boolean},
  {0x209100bb, "CFG-MSGOUT-NMEA_ID_GGA_UART1", Decode::Rate},
  {0x209100ac, "CFG-MSGOUT-NMEA_ID_RMC_UART1", Decode::Rate},
  {0x209100c0, "CFG-MSGOUT-NMEA_ID_GSA_UART1", Decode::Rate},
  {0x209100c5, "CFG-MSGOUT-NMEA_ID_GSV_UART1", Decode::Rate},
  {0x209100ca, "CFG-MSGOUT-NMEA_ID_GLL_UART1", Decode::Rate},
  {0x209100b1, "CFG-MSGOUT-NMEA_ID_VTG_UART1", Decode::Rate},
  {0x209100d9, "CFG-MSGOUT-NMEA_ID_ZDA_UART1", Decode::Rate},
  {0x209100d4, "CFG-MSGOUT-NMEA_ID_GST_UART1", Decode::Rate},
  {0x209100de, "CFG-MSGOUT-NMEA_ID_GBS_UART1", Decode::Rate},
  {0x209100cf, "CFG-MSGOUT-NMEA_ID_GRS_UART1", Decode::Rate},
  {0x209100a7, "CFG-MSGOUT-NMEA_ID_DTM_UART1", Decode::Rate},
  {0x20910401, "CFG-MSGOUT-NMEA_ID_RLM_UART1", Decode::Rate},
  {0x209100e8, "CFG-MSGOUT-NMEA_ID_VLW_UART1", Decode::Rate},
  {0x20910007, "CFG-MSGOUT-UBX_NAV_PVT_UART1", Decode::Rate},
  {0x2091002a, "CFG-MSGOUT-UBX_NAV_POSLLH_UART1", Decode::Rate},
  {0x2091001b, "CFG-MSGOUT-UBX_NAV_STATUS_UART1", Decode::Rate},
  {0x20910016, "CFG-MSGOUT-UBX_NAV_SAT_UART1", Decode::Rate},
  {0x20d00001, "CFG-PM-OPERATEMODE", Decode::Power},
  {0x10d00010, "CFG-PM-LIMITPEAKCURR", Decode::Boolean},
  {0x20110021, "CFG-NAVSPG-DYNMODEL", Decode::Dynamic},
  {0x20110011, "CFG-NAVSPG-FIXMODE", Decode::FixMode},
  {0x201100a2, "CFG-NAVSPG-INFIL_MAXSVS", Decode::Number},
  // MAX-M10S integration manual R04 section 2.1.7: read-only OTP layer 4
  // verification keys. Do NOT send the manual's permanent programming string.
  {0x40a40001, "CPU clock verification key 1", Decode::OtpClock},
  {0x40a40003, "CPU clock verification key 3", Decode::OtpClock},
  {0x40a40005, "CPU clock verification key 5", Decode::OtpClock},
  {0x40a4000a, "CPU clock verification key 10", Decode::OtpClock},
};
constexpr unsigned kKeys = sizeof(keys) / sizeof(keys[0]);
enum class Result : uint8_t { Unavailable, Value, Rejected, Timeout, TxFailure };
struct Value { uint32_t value; Result result; } values[kKeys];
enum class Phase { Idle, Send, Wait, Gap, Report };
Phase phase = Phase::Idle;
unsigned query = 0, reportLine = 0;
unsigned expectedCopies = 1;
uint32_t started = 0, phaseAt = 0, badChecksums = 0, malformed = 0, oversized = 0, partial = 0;
Result versionResult = Result::Unavailable;
uint8_t version[490]; size_t versionLength = 0;
char line[256]; size_t lineLength = 0;
// Fixed storage, including unknown UBX messages. Never scan a UBX payload for '$'.
uint8_t payload[512];
uint16_t length = 0; uint32_t offset = 0, lastFrameByte = 0, frameStarted = 0;
uint8_t step = 0, cls = 0, id = 0, ckA = 0, ckB = 0, receivedA = 0;

void text(const char* p) {
  while (*p && lineLength + 1 < sizeof(line)) line[lineLength++] = *p++;
  line[lineLength] = 0;
}
void number(uint32_t n) {
  char digits[11]; char* p = digits + 10; *p = 0;
  do { *--p = '0' + n % 10; n /= 10; } while (n);
  text(p);
}
void hex(uint32_t n) {
  text("0x");
  for (int shift = 28; shift >= 0; shift -= 4) {
    char c[2] = {"0123456789ABCDEF"[(n >> shift) & 15], 0}; text(c);
  }
}
void hz(uint32_t period) {
  if (!period) { text("unavailable"); return; }
  number(1000 / period); text(".");
  uint32_t fraction = (1000000ULL / period) % 1000;
  if (fraction < 100) text("0");
  if (fraction < 10) text("0");
  number(fraction); text(" Hz");
}
uint32_t le(const uint8_t* p, unsigned size) {
  uint32_t n = 0; for (unsigned i = 0; i < size; ++i) n |= uint32_t(p[i]) << (8 * i); return n;
}
unsigned valueSize(uint32_t key) {
  const unsigned type = key >> 28;
  return type == 1 || type == 2 ? 1 : type == 3 ? 2 : type == 4 ? 4 : type == 5 ? 8 : 0;
}
uint8_t queryLayer(unsigned index) { return keys[index].decode == Decode::OtpClock ? 4 : 0; }
bool navPeriod(uint32_t& period) {
  if (values[0].result != Result::Value || values[1].result != Result::Value ||
      !values[0].value || !values[1].value || values[1].value > 127) return false;
  period = values[0].value * values[1].value; return true;
}
void complete(Result result, uint32_t value = 0) {
  if (query == 0) versionResult = result;
  else values[query - 1] = {value, result};
  ++query; phase = Phase::Gap; phaseAt = millis();
}
void frame() {
  if (phase != Phase::Wait || uint32_t(millis() - phaseAt) >= 800) return;
  if (cls == 5 && id == 0 && length == 2 && payload[0] == (query ? 6 : 10) && payload[1] == (query ? 0x8b : 4)) {
    complete(Result::Rejected); return;
  }
  if (!query) {
    if (cls != 10 || id != 4) return;
    if (length < 40 || (length - 40) % 30 || length > sizeof(version)) { ++malformed; return; }
    memcpy(version, payload, length); versionLength = length;
    complete(Result::Value); return;
  }
  if (cls != 6 || id != 0x8b) return;
  // One distinct key was polled, so only that exact key/value is accepted.
  // This also rejects stale replies, wrong layer/version/position and tails.
  const uint32_t expected = keys[query - 1].id;
  const unsigned size = valueSize(expected);
  if (length < 8 || payload[0] != 1 || payload[1] != queryLayer(query - 1) || payload[2] || payload[3] ||
      le(payload + 4, 4) != expected || length != 4 + expectedCopies * (4 + size)) { ++malformed; return; }
  const uint32_t value = le(payload + 8, size);
  if (expectedCopies == 2 && (le(payload + 8 + size, 4) != expected || le(payload + 12 + size, size) != value)) {
    ++malformed; return;
  }
  if ((expected >> 28) == 1 && value > 1) { ++malformed; return; }
  complete(Result::Value, value);
}
const char* resultName(Result r) {
  return r == Result::Rejected ? "UNSUPPORTED/REJECTED (ACK-NAK)" :
         r == Result::Timeout ? "UNAVAILABLE (response timeout)" :
         r == Result::TxFailure ? "UNAVAILABLE (UART TX incomplete; reset before retry)" : "UNAVAILABLE";
}
const char* dynamic(uint32_t n) {
  switch (n) {
    case 0: return "portable"; case 2: return "stationary"; case 3: return "pedestrian";
    case 4: return "automotive"; case 5: return "sea"; case 6: return "airborne <1g";
    case 7: return "airborne <2g"; case 8: return "airborne <4g"; case 9: return "wrist";
    case 10: return "motorbike"; case 11: return "mower"; case 12: return "e-scooter";
    default: return "unknown enum";
  }
}
void render() {
  lineLength = 0;
  if (!reportLine) text("GPS CONFIG RESULT | current RAM settings + read-only OTP clock verification | Serial1 D7 RX / D6 TX, 38400 baud\n");
  else if (reportLine == 1) {
    text("MON-VER: ");
    if (versionResult != Result::Value) text(resultName(versionResult));
    else {
      text("SW=");
      for (unsigned i = 0; i < 30 && version[i]; ++i) { char c[2] = {version[i] >= 32 && version[i] <= 126 ? char(version[i]) : '?', 0}; text(c); }
      text(" | HW=");
      for (unsigned i = 30; i < 40 && version[i]; ++i) { char c[2] = {version[i] >= 32 && version[i] <= 126 ? char(version[i]) : '?', 0}; text(c); }
    }
    text("\n");
  } else if (reportLine < 2 + 15) {
    const unsigned base = 40 + (reportLine - 2) * 30;
    text("MON-VER extension: ");
    if (base >= versionLength) text("unavailable");
    else for (unsigned i = base; i < base + 30 && version[i]; ++i) { char c[2] = {version[i] >= 32 && version[i] <= 126 ? char(version[i]) : '?', 0}; text(c); }
    text("\n");
  } else if (reportLine < 17 + kKeys) {
    const unsigned index = reportLine - 17;
    const Key& key = keys[index]; const Value& v = values[index];
    text(key.name); text(" ["); hex(key.id); text("] = ");
    if (v.result != Result::Value) text(resultName(v.result));
    else {
      number(v.value); text(" | ");
      switch (key.decode) {
        case Decode::Boolean: text(v.value ? "enabled" : "disabled"); break;
        case Decode::Milliseconds: text("ms measurement interval"); break;
        case Decode::NavRatio: text("measurements/navigation solution"); break;
        case Decode::TimeRef: {
          const char* const refs[] = {"UTC", "GPS", "GLONASS", "BeiDou", "Galileo", "NavIC"};
          text(v.value < 6 ? refs[v.value] : "unknown enum"); break;
        }
        case Decode::Power: text(v.value == 0 ? "FULL (no power save; not proof of high-performance mode)" :
                                 v.value == 1 ? "PSM ON/OFF" : v.value == 2 ? "PSM cyclic tracking" : "unknown enum"); break;
        case Decode::Dynamic: text(dynamic(v.value)); break;
        case Decode::FixMode: text(v.value == 1 ? "2D only" : v.value == 2 ? "3D only" : v.value == 3 ? "auto 2D/3D" : "unknown enum"); break;
        case Decode::OtpClock: text("Hz (OTP layer 4; stored startup clock, not live CPU measurement)"); break;
        case Decode::Rate: {
          if (!v.value) { text("disabled"); break; }
          text("every "); number(v.value); text(" navigation epochs; protocol output gate must also be enabled"); break;
        }
        default: text("unsigned value"); break;
      }
    }
    text("\n");
  } else if (reportLine == 17 + kKeys) {
    text("Configured navigation rate: "); uint32_t period;
    if (navPeriod(period)) { hz(period); text(period <= 100 ? " | >=10Hz REQUESTED" : " | below 10Hz requested"); }
    else text("UNAVAILABLE (missing/invalid MEAS or NAV)");
    text("; use GPS RATE to measure actual NMEA cadence.\n");
  } else if (reportLine == 18 + kKeys) {
    bool known = true, matches = true;
    for (unsigned i = kKeys - 4; i < kKeys; ++i) {
      known = known && values[i].result == Result::Value;
      matches = matches && values[i].value == (i == kKeys - 1 ? 96000000UL : 192000000UL);
    }
    text("High-clock OTP profile: "); text(!known ? "UNAVAILABLE" : matches ? "matches documented high-performance profile" : "does not match documented high-performance profile");
    text(". 10Hz support NOT PROVEN: depends on firmware, signals and applied CPU clock; FULL power mode is insufficient.\n");
  } else if (reportLine == 19 + kKeys) {
    text("UART 38400 8N1 budget=3840 bytes/s. NMEA divisors do not specify bytes/s; measure GPS RATE before increasing output.\n");
  } else {
    text("GPS CONFIG END | bad_checksum="); number(badChecksums); text(" | malformed="); number(malformed);
    text(" | oversized="); number(oversized); text(" | partial_timeout="); number(partial);
    text(" | GPS/flash settings unchanged; normal reception continues.\n");
  }
}
}
}

namespace GpsConfig {
bool active() { return Config::phase != Config::Phase::Idle; }
bool start() {
  using namespace Config;
  if (active() || GpsProfile::active() || Recorder::exclusive() || !FlashWriteTest::diagnosticsAllowed()) return false;
  memset(values, 0, sizeof(values)); versionLength = 0; versionResult = Result::Unavailable;
  query = reportLine = 0; badChecksums = malformed = oversized = partial = 0;
  started = phaseAt = millis(); lineLength = 0; phase = Phase::Send;
  return true;
}
bool consume(uint8_t value) {
  using namespace Config;
  // Always demultiplex UBX, including late replies after CONFIG has finished.
  if (step && (uint32_t(millis() - lastFrameByte) >= 100 || uint32_t(millis() - frameStarted) >= 250)) {
    step = 0; if (active()) ++partial;
  }
  lastFrameByte = millis();
  if (step == 0) { if (value != 0xb5) return false; step = 1; frameStarted = millis(); return true; }
  if (step == 1) {
    if (value == 0x62) { step = 2; ckA = ckB = 0; return true; }
    step = value == 0xb5 ? 1 : 0; return value == 0xb5;
  }
  if (step == 2) cls = value;
  else if (step == 3) id = value;
  else if (step == 4) length = value;
  else if (step == 5) {
    length |= uint16_t(value) << 8; offset = 0;
    if (length > sizeof(payload) && active()) ++oversized;
  } else if (step == 6) {
    if (offset < sizeof(payload)) payload[offset] = value;
    ++offset;
  } else if (step == 7) { receivedA = value; step = 8; return true; }
  else {
    step = 0;
    if (receivedA == ckA && value == ckB) {
      if (length <= sizeof(payload)) { GpsProfile::onFrame(cls, id, payload, length); frame(); }
    }
    else if (active()) ++badChecksums;
    return true;
  }
  ckA += value; ckB += ckA;
  if (step < 5) ++step;
  else if (step == 5) step = length ? 6 : 7;
  else if (offset == length) step = 7;
  return true;
}
void update() {
  using namespace Config;
  if (!active()) return;
  const uint32_t now = millis();
  if (step && (uint32_t(now - lastFrameByte) >= 100 || uint32_t(now - frameStarted) >= 250)) { step = 0; ++partial; }
  if (phase != Phase::Report && uint32_t(now - started) >= 55000) {
    phase = Phase::Report; // Unattempted fields stay explicitly unavailable.
  }
  if (phase == Phase::Gap && uint32_t(now - phaseAt) >= 200) {
    phase = query == kKeys + 1 ? Phase::Report : Phase::Send; phaseAt = now;
  }
  if (phase == Phase::Send) {
    if (!GpsUartTx::ready()) {
      if (uint32_t(now - phaseAt) >= 1000) { complete(Result::TxFailure); phase = Phase::Report; }
      return;
    }
    // Hard-coded allowlist: MON-VER empty poll or CFG-VALGET version 0,
    // RAM layer 0 (OTP layer 4 only for documented clock verification),
    // position 0, one distinct key. Alternate 16/20-byte requests by polling
    // that same key twice (official VALGET duplicate semantics). Different DMA
    // lengths prevent a stale TXD.AMOUNT from the previous transfer being
    // mistaken for completion of a new, stuck transfer in the Seeed BSP.
    expectedCopies = query && GpsUartTx::previousSize() == 16 ? 2 : 1;
    const unsigned count = query ? 12 + expectedCopies * 4 : 8;
    uint8_t packet[20] = {0xb5, 0x62, uint8_t(query ? 6 : 10), uint8_t(query ? 0x8b : 4), uint8_t(count - 8), 0};
    if (query) {
      packet[7] = queryLayer(query - 1);
      for (unsigned copy = 0; copy < expectedCopies; ++copy)
        for (unsigned i = 0; i < 4; ++i) packet[10 + copy * 4 + i] = keys[query - 1].id >> (8 * i);
    }
    uint8_t a = 0, b = 0;
    for (unsigned i = 2; i < count - 2; ++i) { a += packet[i]; b += a; }
    packet[count - 2] = a; packet[count - 1] = b;
    if (GpsUartTx::write(packet, count) != count) { complete(Result::TxFailure); phase = Phase::Report; return; }
    phase = Phase::Wait; phaseAt = now;
  } else if (phase == Phase::Wait && uint32_t(now - phaseAt) >= 800) complete(Result::Timeout);
  if (phase != Phase::Report || !Serial || Recorder::exporting()) return;
  // Skip nonexistent MON-VER extensions without emitting fifteen empty lines.
  while (reportLine >= 2 && reportLine < 17 && 40 + (reportLine - 2) * 30 >= versionLength) ++reportLine;
  if (!lineLength) render();
  if (Serial.availableForWrite() < int(lineLength)) return;
  if (Serial.write(reinterpret_cast<const uint8_t*>(line), lineLength) != lineLength) return;
  lineLength = 0;
  if (++reportLine == 21 + kKeys) phase = Phase::Idle;
}
}
