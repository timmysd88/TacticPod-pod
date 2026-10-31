#include "Arduino.h"
#include "SPI.h"
#include "../../src/flash_write_test.h"
uint32_t mockTime = 0;
unsigned mockRedWrites = 0, mockImuAxisReads = 0;
MockSerial Serial;
MockUart Serial1;
MockUarte mockUarte;
void mockStopRx() {
  ++mockUarte.stopCalls;
  if (!mockUarte.stuckStop) { mockUarte.EVENTS_RXTO = 1; mockUarte.EVENTS_RXSTARTED = 0; }
}
void mockStartRx() {
  ++mockUarte.startCalls;
  if (!mockUarte.stuckStart) { mockUarte.EVENTS_RXSTARTED = 1; mockUarte.EVENTS_RXTO = 0; }
}
MockSPI SPI;
bool gpsTestDiagnosticAvailable = true;
namespace FlashWriteTest {
bool diagnosticsAllowed() { return gpsTestDiagnosticAvailable; }
void update() {}
}
#include "../../src/recorder.cpp"
#include "../../src/monotonic_clock.cpp"
#include "../../src/compact_recorder.cpp"
#include "../../src/ram_observations.cpp"
#include "../../src/gps_uart_scan.cpp"
#include "../../src/gps_rate.cpp"
#include "../../src/gps_config.cpp"
#include "../../src/gps_profile.cpp"
#include "../../src/gps_receiver.cpp"
#include "../../src/serial_console.cpp"
#include "../../src/main.cpp"

char sentence[256];
void prepareNmea(const char* body, bool good = true) {
  unsigned i = 0; uint8_t checksum = 0;
  sentence[i++] = '$';
  while (*body) { checksum ^= *body; sentence[i++] = *body++; }
  if (!good) checksum ^= 1;
  sentence[i++] = '*'; sentence[i++] = "0123456789ABCDEF"[checksum >> 4];
  sentence[i++] = "0123456789ABCDEF"[checksum & 15];
  sentence[i++] = '\r'; sentence[i++] = '\n'; sentence[i] = 0;
  Serial1.rx = sentence;
}
void nmea(const char* body, bool good = true) {
  prepareNmea(body, good);
  while (Serial1.available()) GpsReceiver::update();
}
void validFix(bool gn = false) {
  nmea(gn ? "GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,," :
            "GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  nmea(gn ? "GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W" :
            "GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W");
}
void drain() { for (unsigned i = 0; i < 10; ++i) GpsReceiver::update(); }
void clear() { Serial.used = 0; Serial.output[0] = 0; }
bool contains(const char* text) { return strstr(Serial.output, text) != nullptr; }
unsigned reports() {
  unsigned result = 0; const char* p = Serial.output;
  while ((p = strstr(p, "GPS STATUS |"))) { ++result; ++p; }
  return result;
}
void statusGps() { SerialConsole::handleCommand("GPS STATUS"); drain(); }
void binaryData(const uint8_t* bytes, size_t count) {
  Serial1.binary = bytes; Serial1.binaryRemaining = count; Serial1.rx = "";
  while (Serial1.available()) GpsReceiver::update();
}
void finishRaw() { mockTime = rawStartedAt + 10000; for (unsigned i = 0; i < 2000; ++i) GpsReceiver::update(); }
void prepareRateNmea(bool isGga, uint32_t utcMs, bool fix = false, bool gp = false, bool good = true) {
  utcMs %= 86400000UL;
  const unsigned hour = utcMs / 3600000, minute = (utcMs / 60000) % 60, second = (utcMs / 1000) % 60;
  char time[11] = {char('0' + hour / 10), char('0' + hour % 10), char('0' + minute / 10), char('0' + minute % 10),
                   char('0' + second / 10), char('0' + second % 10), '.', char('0' + (utcMs / 100) % 10),
                   char('0' + (utcMs / 10) % 10), char('0' + utcMs % 10), 0};
  char body[180]; strcpy(body, gp ? "GP" : "GN"); strcat(body, isGga ? "GGA," : "RMC,"); strcat(body, time);
  strcat(body, isGga ? (fix ? ",4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,," : ",,,,,0,00,99.99,,,,,,") :
                       (fix ? ",A,4807.038,N,01131.000,E,022.4,084.4,010100,003.1,W" : ",V,,,,,,,010100,,,"));
  prepareNmea(body, good);
}
void rateFrame(bool ggaType, uint32_t utcMs, bool fix = false, bool gp = false, bool good = true) {
  prepareRateNmea(ggaType, utcMs, fix, gp, good);
  while (Serial1.available()) GpsReceiver::update();
}
void finishRate() { mockTime = Rate::started + 15000; for (unsigned i = 0; i < 30; ++i) GpsReceiver::update(); }

uint8_t configFrame[650];
size_t makeUbx(uint8_t cls, uint8_t id, const uint8_t* data, size_t length, bool good = true) {
  configFrame[0] = 0xb5; configFrame[1] = 0x62; configFrame[2] = cls; configFrame[3] = id;
  configFrame[4] = length; configFrame[5] = length >> 8;
  memcpy(configFrame + 6, data, length);
  uint8_t a = 0, b = 0;
  for (unsigned i = 2; i < length + 6; ++i) { a += configFrame[i]; b += a; }
  configFrame[length + 6] = a ^ (good ? 0 : 1); configFrame[length + 7] = b;
  return length + 8;
}
void ubx(uint8_t cls, uint8_t id, const uint8_t* data, size_t length, bool good = true) {
  const size_t count = makeUbx(cls, id, data, length, good); binaryData(configFrame, count);
}
void configReply(uint32_t value = 1, bool good = true, unsigned variant = 0) {
  if (!Config::query) {
    uint8_t version[100] = {};
    memcpy(version, "ROM CORE 5.00", 13); memcpy(version + 30, "000A0000", 8);
    memcpy(version + 40, "FWVER=SPG 5.00", 13); memcpy(version + 70, "PROTVER=34.00", 13);
    ubx(10, 4, version, sizeof(version), good); return;
  }
  const uint32_t key = Config::keys[Config::query - 1].id;
  const unsigned size = Config::valueSize(key);
  uint8_t reply[20] = {1, 0, 0, 0};
  reply[1] = Config::queryLayer(Config::query - 1);
  for (unsigned i = 0; i < 4; ++i) reply[4 + i] = key >> (8 * i);
  for (unsigned i = 0; i < size; ++i) reply[8 + i] = value >> (8 * i);
  if (Config::expectedCopies == 2) memcpy(reply + 8 + size, reply + 4, 4 + size);
  if (variant == 1) reply[0] = 0;
  if (variant == 2) reply[1] = 1;
  if (variant == 3) reply[2] = 1;
  if (variant == 4) reply[4] ^= 1;
  ubx(6, 0x8b, reply, 4 + Config::expectedCopies * (4 + size) + (variant == 5 ? 1 : 0), good);
}
void runConfig(bool reply = true, bool fast = false) {
  for (unsigned i = 0; i < 47000 && GpsConfig::active(); ++i) {
    ++mockTime; GpsReceiver::update();
    if (reply && Config::phase == Config::Phase::Wait) {
      const uint32_t key = Config::query ? Config::keys[Config::query - 1].id : 0;
      configReply(key == 0x30210001 ? (fast ? 100 : 1000) : key == 0x40520001 ? 38400 :
                  key == 0x20210003 ? 0 : key == 0x20d00001 ? 0 : key == 0x20110021 ? 3 : key == 0x20110011 ? 3 : 1);
    }
    if (Config::phase == Config::Phase::Report && (!Serial || !Serial.freeSpace)) return;
  }
}
bool onlyReadPolls() {
  for (unsigned packet = 0; packet < Serial1.writeCalls; ++packet) {
    if (packet >= 64) return false;
    const uint8_t* p = Serial1.writes[packet]; const size_t length = Serial1.writeSizes[packet];
    if (p[0] != 0xb5 || p[1] != 0x62) return false;
    if (!((length == 8 && p[2] == 10 && p[3] == 4 && p[4] == 0 && p[5] == 0) ||
          ((length == 16 || length == 20) && p[2] == 6 && p[3] == 0x8b && p[4] == length - 8 && p[5] == 0 &&
           p[6] == 0 && (p[7] == 0 || p[7] == 4) && p[8] == 0 && p[9] == 0))) return false;
    if (length == 16 || length == 20) {
      const uint32_t key = uint32_t(p[10]) | uint32_t(p[11]) << 8 | uint32_t(p[12]) << 16 | uint32_t(p[13]) << 24;
      const bool clockKey = key == 0x40a40001 || key == 0x40a40003 || key == 0x40a40005 || key == 0x40a4000a;
      if (p[7] != (clockKey ? 4 : 0)) return false;
      if (length == 20 && memcmp(p + 10, p + 14, 4)) return false;
    }
    uint8_t a = 0, b = 0;
    for (unsigned i = 2; i < length - 2; ++i) { a += p[i]; b += a; }
    if (a != p[length - 2] || b != p[length - 1]) return false;
  }
  return SPI.erases == 0 && SPI.programs == 0 && SPI.violations == 0;
}

int configScenario(unsigned scenario) {
  using namespace Config;
  if (scenario == 82) {
    const Stage stages[] = {Stage::Scan, Stage::CheckBlank, Stage::Header, Stage::Record, Stage::Footer,
                            Stage::Erase, Stage::EraseVerify, Stage::ExportHeader, Stage::ExportPage, Stage::Fault};
    for (Stage s : stages) {
      stage = s; SerialConsole::handleCommand("GPS CONFIG");
      if (GpsConfig::active()) return 120;
    }
    stage = Stage::Idle; gpsTestDiagnosticAvailable = false;
    SerialConsole::handleCommand("GPS CONFIG");
    return !GpsConfig::active() && !Serial1.writeCalls ? 0 : 121;
  }
  if (scenario == 84) {
    SerialConsole::handleCommand("GPS RATE"); SerialConsole::handleCommand("GPS CONFIG");
    if (GpsConfig::active()) return 122;
    Rate::phase = Rate::Phase::Idle;
    SerialConsole::handleCommand("GPS RAW"); SerialConsole::handleCommand("GPS CONFIG");
    if (GpsConfig::active()) return 123;
    rawPhase = RawPhase::Idle;
    SerialConsole::handleCommand("UART LOOPBACK TEST"); SerialConsole::handleCommand("GPS CONFIG");
    if (GpsConfig::active()) return 124;
    loopPhase = LoopPhase::Idle;
    SerialConsole::handleCommand("GPS SCAN"); SerialConsole::handleCommand("GPS CONFIG");
    return !GpsConfig::active() && !Serial1.writeCalls ? 0 : 125;
  }
  if (scenario == 89) mockTime = 0xffffff00UL;
  SerialConsole::handleCommand("GPS CONFIG");
  if (!GpsConfig::active() || !GpsReceiver::uartScanBusy()) return 126;
  GpsReceiver::update();
  if (scenario == 73 || scenario == 89) {
    runConfig();
    const uint8_t expected[] = {0xb5, 0x62, 6, 0x8b, 8, 0, 0, 0, 0, 0, 1, 0, 0x21, 0x30, 0xeb, 0x07};
    // Independently golden-encoded first CFG-RATE-MEAS poll, checksum included.
    if (memcmp(Serial1.writes[1], expected, sizeof(expected))) return 127;
    return !GpsConfig::active() && onlyReadPolls() && Serial1.writeCalls == kKeys + 1 &&
      contains("CFG-RATE-MEAS [0x30210001] = 1000 | ms") && contains("1.000 Hz") &&
      contains("PROTVER=34.00") && contains("pedestrian") && contains("GPS CONFIG END") ? 0 : 128;
  }
  if (scenario == 74) {
    const uint8_t nakVersion[] = {10, 4}; ubx(5, 0, nakVersion, 2);
    mockTime += 200; GpsReceiver::update();
    const uint8_t nak[] = {6, 0x8b}; ubx(5, 0, nak, 2);
    if (values[0].result != Result::Rejected) return 129;
    runConfig();
    return contains("UNSUPPORTED/REJECTED") && values[1].result == Result::Value && onlyReadPolls() ? 0 : 130;
  }
  if (scenario == 75) {
    runConfig(false);
    return !GpsConfig::active() && contains("response timeout") && contains("GPS CONFIG END") &&
      !contains("UNSUPPORTED/REJECTED") && Serial1.writeCalls == kKeys + 1 && onlyReadPolls() ? 0 : 131;
  }
  if (scenario == 100) {
    uint8_t data[512] = {};
    ubx(10, 4, data, 39); ubx(10, 4, data, 41); ubx(10, 4, data, 512);
    if (malformed != 3 || query != 0) return 158;
    memset(data, 'X', 490); data[0] = 0x1b; data[30] = 1;
    ubx(10, 4, data, 490); runConfig();
    return versionLength == 490 && contains("SW=?XXX") && !strchr(Serial.output, 0x1b) && onlyReadPolls() ? 0 : 159;
  }
  configReply(); mockTime += 200; GpsReceiver::update();
  if (phase != Phase::Wait || query != 1) return 132;
  if (scenario == 76) {
    configReply(100, false);
    if (query != 1 || badChecksums != 1) return 133;
    configReply(100); runConfig();
    return values[0].value == 100 && contains("10.000 Hz") && onlyReadPolls() ? 0 : 134;
  }
  if (scenario == 77) {
    for (unsigned variant = 1; variant <= 5; ++variant) configReply(100, true, variant);
    if (query != 1 || malformed != 5) return 135;
    configReply(100); runConfig();
    return values[0].value == 100 && onlyReadPolls() ? 0 : 136;
  }
  if (scenario == 78) {
    validFix(); const uint32_t before = gps.passedChecksum();
    prepareNmea("GNGGA,123519,,,,,0,00,99.99,,,,,,");
    uint8_t data[256]; const size_t count = strlen(sentence); memcpy(data, sentence, count);
    ubx(0x99, 0x55, data, count);
    if (gps.passedChecksum() != before || query != 1) return 137;
    configReply(100); runConfig(); validFix(); statusGps();
    return gps.passedChecksum() == before + 2 && contains("GPS fix=VALID") && onlyReadPolls() ? 0 : 138;
  }
  if (scenario == 79) {
    const uint8_t data[] = {1, 0, 0, 0, 1, 0, 0x21, 0x30, 100, 0};
    const size_t count = makeUbx(6, 0x8b, data, sizeof(data));
    for (unsigned i = 0; i < count; ++i) { binaryData(configFrame + i, 1); ++mockTime; }
    runConfig(); return values[0].value == 100 && !badChecksums && onlyReadPolls() ? 0 : 139;
  }
  if (scenario == 80) {
    uint8_t data[600]; memset(data, '$', sizeof(data)); ubx(6, 0x8b, data, sizeof(data));
    if (oversized != 1 || query != 1) return 140;
    configReply(100); runConfig(); return values[0].value == 100 && onlyReadPolls() ? 0 : 141;
  }
  if (scenario == 81) {
    const uint8_t prefix[] = {0xb5, 0x62, 6, 0x8b, 0xff, 0xff}; binaryData(prefix, sizeof(prefix));
    mockTime += 100; GpsReceiver::update();
    validFix(); configReply(100); runConfig();
    return partial == 1 && gps.passedChecksum() == 2 && values[0].value == 100 && onlyReadPolls() ? 0 : 142;
  }
  if (scenario == 83) {
    const char* const commands[] = {"RECORD START", "RECORD ERASE", "RECORD ERASE CONFIRM", "RECORD READ", "ERASE FFF000", "DIAG", "GPS SCAN", "GPS RAW", "GPS RATE", "UART LOOPBACK TEST", "GPS CONFIG"};
    const unsigned saved = query;
    for (const char* cmd : commands) SerialConsole::handleCommand(cmd);
    if (query != saved || GpsRate::active() || GpsUartScan::active() || rawPhase != RawPhase::Idle || loopPhase != LoopPhase::Idle) return 143;
    runConfig(); return onlyReadPolls() && !Recorder::recording() ? 0 : 144;
  }
  if (scenario == 85 || scenario == 86) {
    Serial.freeSpace = scenario == 85 ? 0 : 256; Serial.connected = scenario != 86;
    clear(); runConfig();
    if (phase != Phase::Report || Serial.used || Serial.overfilledWrites) return 145;
    Serial.freeSpace = 256; Serial.connected = true; drain();
    for (unsigned i = 0; i < 100; ++i) GpsReceiver::update();
    return !GpsConfig::active() && contains("GPS CONFIG END") && onlyReadPolls() && !Serial.overfilledWrites ? 0 : 146;
  }
  if (scenario == 87) {
    // Simulate an in-flight, stuck DMA; never enter BSP's blocking write again.
    mockUarte.TXD.AMOUNT = 0;
    configReply(100); runConfig();
    return !GpsConfig::active() && Serial1.writeCalls == 2 && contains("UART TX incomplete") && onlyReadPolls() ? 0 : 147;
  }
  if (scenario == 88) {
    configReply(100); Serial1.shortTx = true; runConfig();
    return !GpsConfig::active() && contains("UART TX incomplete") && onlyReadPolls() ? 0 : 148;
  }
  if (scenario == 90) {
    runConfig(true, true);
    memset(SPI.memory, 0xff, sizeof(SPI.memory)); memset(SPI.finalSector, 0xff, sizeof(SPI.finalSector));
    Serial.connected = false; setup(); startupPending = false; // Explicit-diagnostic regression, boot covered separately.
    for (unsigned i = 0; i < 4100; ++i) { ++mockTime; loop(); }
    SerialConsole::handleCommand("RECORD START");
    for (unsigned i = 0; i < 1000; ++i) {
      if (i % 100 == 0) {
        const uint8_t data[64] = {}; const size_t count = makeUbx(1, 7, data, sizeof(data));
        Serial1.binary = configFrame; Serial1.binaryRemaining = count;
      }
      ++mockTime; loop();
    }
    SerialConsole::handleCommand("GPS CONFIG");
    if (GpsConfig::active()) return 149;
    SerialConsole::handleCommand("RECORD STOP");
    for (unsigned i = 0; i < 600; ++i) { ++mockTime; loop(); }
    return committed == 100 && missed == 0 && lost == 0 && sizeof(RecordFormat::Sample) == 16 && SPI.violations == 0 ? 0 : 150;
  }
  if (scenario == 91) {
    runConfig(); Serial1.loopEcho = true; clear(); SerialConsole::handleCommand("UART LOOPBACK TEST");
    for (unsigned i = 0; i < 1200; ++i) { ++mockTime; GpsReceiver::update(); }
    return contains("UART LOOPBACK TEST PASS") && Serial1.baud == 38400 ? 0 : 151;
  }
  if (scenario == 92) {
    runConfig(); clear(); SerialConsole::handleCommand("GPS SCAN");
    for (unsigned i = 0; i < 20000; ++i) { ++mockTime; GpsReceiver::update(); }
    return contains("GPS SCAN END") && !GpsUartScan::active() && onlyReadPolls() ? 0 : 152;
  }
  if (scenario == 93) {
    // A 250 ms absolute frame timeout also bounds corrupt long frames under
    // continuous traffic (no inter-byte gap).
    const uint8_t prefix[] = {0xb5, 0x62, 6, 0x8b, 0xff, 0xff}; binaryData(prefix, sizeof(prefix));
    for (unsigned i = 0; i < 260; ++i) { ++mockTime; const uint8_t x = 'X'; binaryData(&x, 1); }
    validFix(); configReply(100); runConfig();
    return partial == 1 && gps.passedChecksum() == 2 && values[0].value == 100 && onlyReadPolls() ? 0 : 153;
  }
  if (scenario == 94) {
    mockTime = phaseAt + 800; configReply(100); GpsReceiver::update();
    if (values[0].result != Result::Timeout) return 154;
    // Late reply for previous key must not satisfy the next query.
    mockTime += 200; GpsReceiver::update();
    const uint8_t stale[] = {1, 0, 0, 0, 1, 0, 0x21, 0x30, 100, 0}; ubx(6, 0x8b, stale, sizeof(stale));
    if (query != 2) return 155;
    runConfig(); return values[0].result == Result::Timeout && onlyReadPolls() ? 0 : 156;
  }
  if (scenario == 95 || scenario == 96) {
    for (unsigned i = 0; i < 15000 && GpsConfig::active(); ++i) {
      ++mockTime; GpsReceiver::update();
      if (phase != Phase::Wait) continue;
      if (query && keys[query - 1].decode == Decode::OtpClock) {
        if (scenario == 96) { const uint8_t nak[] = {6, 0x8b}; ubx(5, 0, nak, 2); }
        else configReply(query == kKeys ? 96000000 : 192000000);
      } else configReply();
    }
    return !GpsConfig::active() && onlyReadPolls() && contains(scenario == 95 ?
      "High-clock OTP profile: matches documented high-performance profile" : "High-clock OTP profile: UNAVAILABLE") &&
      contains("10Hz support NOT PROVEN") ? 0 : 160;
  }
  if (scenario == 97) {
    configReply(100); mockTime += 200; GpsReceiver::update();
    if (expectedCopies != 2) return 161;
    const uint8_t disagree[] = {1, 0, 0, 0, 2, 0, 0x21, 0x30, 1, 0, 2, 0, 0x21, 0x30, 2, 0};
    ubx(6, 0x8b, disagree, sizeof(disagree));
    if (query != 2 || malformed != 1) return 162;
    runConfig(); return !GpsConfig::active() && onlyReadPolls() ? 0 : 163;
  }
  if (scenario == 98) {
    configReply(100); mockUarte.TXD.AMOUNT = 16; Serial1.stuckTx = true;
    mockTime += 200; GpsReceiver::update();
    if (GpsUartTx::previousSize() != 20 || mockUarte.TXD.AMOUNT != 16) return 164;
    configReply(1); runConfig();
    return !GpsConfig::active() && Serial1.writeCalls == 3 && contains("UART TX incomplete") && onlyReadPolls() ? 0 : 165;
  }
  if (scenario == 99) {
    runConfig(); clear(); SerialConsole::handleCommand("GPS RAW");
    const uint8_t data[] = {0, 0x1b, 0xff, '$'}; ubx(1, 7, data, sizeof(data));
    finishRaw();
    return rawTotal == 12 && rawValidSentences == 0 && contains("bytes=12") && onlyReadPolls() ? 0 : 166;
  }
  return 157;
}

#include "profile_cases.h"
#include "observation_cases.h"

int mixedRateScenario(unsigned scenario) {
  startupPending = false;
  SerialConsole::handleCommand("GPS RATE");
  const unsigned count = scenario == 155 ? 15 : scenario == 154 ? 149 : 150;
  const bool noFix = scenario == 156 || scenario == 157;
  for (unsigned i = 0; i < 150; ++i) {
    mockTime = Rate::started + i * 100;
    if (i % 10 == 0) rateFrame(true, i * 100, !noFix);
    if ((scenario == 155 && i % 10 != 0) || (scenario == 154 && i >= count)) continue;
    const unsigned epoch = i * 100;
    if (scenario == 156) nmea("GNRMC,,V,,,,,,,010100,,,");
    else if (scenario == 159) nmea("GNRMC,,A,4807.038,N,01131.000,E,022.4,084.4,010100,,,");
    else rateFrame(false, scenario == 158 ? 0 : epoch, !noFix);
  }
  // Complete a checksum-valid frame exactly at the end: it must be excluded.
  mockTime = Rate::started + 15000;
  rateFrame(false, 15000, true);
  finishRate();
  if (Rate::rmc.sentences != count || Rate::gga.sentences != 15 ||
      !contains("primary=RMC") || !contains("role=SUPPORTING") ||
      !contains("GGA/RMC epochs are NOT summed") || Serial.overfilledWrites ||
      Serial1.writeCalls || SPI.erases || SPI.programs || SPI.violations) return 270;
  if (scenario == 156 || scenario == 159)
    return Rate::rmc.epochs == 0 && Rate::rmc.missingUtc == 150 &&
      Rate::gga.epochs == 15 && contains("output_cadence_10hz=UNKNOWN") ? 0 : 271;
  if (scenario == 158)
    return Rate::rmc.epochs == 1 && Rate::rmc.duplicates == 149 &&
      contains("output_cadence_10hz=UNKNOWN") ? 0 : 272;
  if (Rate::rmc.epochs != count || Rate::gga.epochs != 15) return 273;
  if (scenario == 157 && (!contains("fixes=NONE_OBSERVED") || Rate::rmc.fixes ||
      !contains("output cadence does not prove valid 10 Hz navigation solutions"))) return 274;
  if (scenario == 154 && !contains("utc_epoch_hz=9.933")) return 275;
  return contains(scenario == 155 ? "output_cadence_10hz=NO" : "output_cadence_10hz=YES") ? 0 : 276;
}

int runScenario(unsigned scenario) {
  GpsReceiver::begin();
  if (scenario >= 160) return observationScenario(scenario);
  if (scenario >= 153) return mixedRateScenario(scenario);
  // Legacy cases exercise explicit diagnostics independently of the boot job.
  if (scenario < 145) startupPending = false;
  if (scenario >= 145) return startupScenario(scenario);
  if (Serial1.baud != 38400 || printing) return 1;
  if (mockUarte.BAUDRATE != UARTE_BAUDRATE_BAUDRATE_Baud38400) return 80;
  if (scenario == 0) {
    mockTime = 6000; drain();
    if (Serial.used) return 2;
    statusGps();
    return contains("GPS ERROR: no UART data") && contains("last_valid_message_age_ms=N/A") && contains("lat=N/A") ? 0 : 3;
  }
  mockTime = 1000;
  if (scenario >= 101) return profileScenario(scenario);
  if (scenario >= 73) return configScenario(scenario);
  if (scenario >= 55 && scenario < 73) {
    if (scenario <= 59) {
      SerialConsole::handleCommand("GPS RATE");
      const unsigned epochs = scenario == 55 || scenario == 58 ? 15 : 150;
      for (unsigned i = 0; i < epochs; ++i) {
        mockTime = Rate::started + i * (15000 / epochs);
        rateFrame(true, i * (15000 / epochs), scenario == 57);
        if (scenario == 59) rateFrame(true, i * 100, false, true);
        rateFrame(false, i * (15000 / epochs), scenario == 57);
        if (scenario == 58) rateFrame(false, i * 1000, false, false, false);
      }
      finishRate();
      if (Rate::gga.epochs != epochs || Rate::rmc.epochs != epochs || Rate::gga.sentences != epochs * (scenario == 59 ? 2 : 1) || Rate::rmc.sentences != epochs) return 83;
      if (scenario == 58 && Rate::badChecksum != 15) return 84;
      if (scenario == 59 && Rate::gga.duplicates != 150) return 85;
      if (scenario == 57 && Rate::gga.fixes != 150) return 86;
      if (scenario != 57 && (Rate::gga.fixes || Rate::gga.noFix != Rate::gga.sentences)) return 87;
      return contains(epochs == 150 ? "output_cadence_10hz=YES" : "output_cadence_10hz=NO") &&
        contains("GGA/RMC epochs are NOT summed") && !GpsRate::active() ? 0 : 88;
    }
    if (scenario == 60) {
      SerialConsole::handleCommand("GPS RATE");
      for (unsigned i = 0; i < 150; ++i) { mockTime = Rate::started + i * 100; rateFrame(false, i * 100); }
      finishRate();
      return Rate::gga.sentences == 0 && Rate::rmc.epochs == 150 && contains("primary=RMC") &&
        contains("output_cadence_10hz=YES") && contains("GGA_RMC_only_at10Hz_Bps=N/A") ? 0 : 89;
    }
    if (scenario == 61 || scenario == 62) {
      SerialConsole::handleCommand("GPS RATE");
      for (unsigned i = 0; i < 150; ++i) {
        mockTime = Rate::started + i * 100;
        if (scenario == 61) rateFrame(true, 0);
        else nmea("GNGGA,,,,,,0,00,99.99,,,,,,");
      }
      finishRate();
      return Rate::gga.sentences == 150 && (scenario == 61 ? Rate::gga.epochs == 1 && Rate::gga.duplicates == 149 : Rate::gga.epochs == 0 && Rate::gga.missingUtc == 150) &&
        contains("output_cadence_10hz=UNKNOWN") ? 0 : 90;
    }
    if (scenario == 63) {
      SerialConsole::handleCommand("GPS RATE"); finishRate();
      return Rate::bytes == 0 && contains("primary=NONE") && contains("fixes=NONE_OBSERVED") && contains("output_cadence_10hz=UNKNOWN") ? 0 : 91;
    }
    if (scenario == 64) {
      SerialConsole::handleCommand("GPS RATE");
      mockTime = Rate::started + 14999; rateFrame(true, 0);
      mockTime = Rate::started + 15000; rateFrame(false, 0); finishRate();
      return Rate::gga.sentences == 1 && Rate::rmc.sentences == 0 && gps.passedChecksum() == 2 ? 0 : 92;
    }
    if (scenario == 65 || scenario == 66) {
      SerialConsole::handleCommand("GPS RATE");
      if (scenario == 65) Serial.freeSpace = 0; else Serial.connected = false;
      for (unsigned i = 0; i < 15; ++i) { mockTime = Rate::started + i * 1000; rateFrame(true, i * 1000); }
      finishRate();
      if (Rate::phase != Rate::Phase::Report || Serial.used) return 93;
      Serial.freeSpace = 256; Serial.connected = true; drain();
      return !GpsRate::active() && Rate::gga.epochs == 15 && contains("GPS RATE END") && Serial.overfilledWrites == 0 ? 0 : 94;
    }
    if (scenario == 67) {
      stage = Stage::ExportHeader; SerialConsole::handleCommand("GPS RATE");
      if (GpsRate::active() || !contains("refused")) return 95;
      stage = Stage::Idle; SerialConsole::handleCommand("GPS RATE"); clear();
      rateFrame(true, 0); clear(); stage = Stage::ExportHeader; finishRate();
      if (Serial.used || !GpsRate::active()) return 96;
      stage = Stage::Idle; drain();
      return !GpsRate::active() && contains("GPS RATE END") ? 0 : 97;
    }
    if (scenario == 68) {
      SerialConsole::handleCommand("GPS RATE"); const uint32_t start = Rate::started;
      mockTime += 1000; SerialConsole::handleCommand("GPS RATE");
      SerialConsole::handleCommand("GPS RAW"); SerialConsole::handleCommand("GPS SCAN");
      SerialConsole::handleCommand("UART LOOPBACK TEST"); SerialConsole::handleCommand("GPS STATUS");
      SerialConsole::handleCommand("GPS OFF");
      if (Rate::started != start || rawPhase != RawPhase::Idle || loopPhase != LoopPhase::Idle || GpsUartScan::active() || Serial1.writeCalls) return 98;
      finishRate(); return !GpsRate::active() && !printing && contains("wait for GPS RATE END") ? 0 : 99;
    }
    if (scenario == 69) {
      memset(SPI.memory, 0xFF, sizeof(SPI.memory)); memset(SPI.finalSector, 0xFF, sizeof(SPI.finalSector));
      Serial.connected = false; setup(); startupPending = false; // Explicit-diagnostic regression, boot covered separately.
      for (unsigned i = 0; i < 4100; ++i) { ++mockTime; loop(); }
      SerialConsole::handleCommand("RECORD START"); SerialConsole::handleCommand("GPS RATE");
      static char pair[512];
      for (unsigned i = 0; i < 15000; ++i) {
        if (i % 100 == 0) {
          prepareRateNmea(true, i); strcpy(pair, sentence);
          prepareRateNmea(false, i); strcat(pair, sentence); Serial1.rx = pair;
        }
        loop(); ++mockTime;
      }
      SerialConsole::handleCommand("RECORD STOP");
      for (unsigned i = 0; i < 600; ++i) { ++mockTime; loop(); }
      if (committed != 1500 || missed || lost || Rate::gga.epochs != 150 || Rate::rmc.epochs != 150 || SPI.erases || SPI.violations) return 100;
      Serial.connected = true; drain();
      return contains("output_cadence_10hz=YES") && sizeof(RecordFormat::Sample) == 16 ? 0 : 101;
    }
    if (scenario == 70) {
      mockTime = 0xFFFFE000; SerialConsole::handleCommand("GPS RATE");
      for (unsigned i = 0; i < 15; ++i) {
        mockTime = Rate::started + i * 1000; rateFrame(true, 86399000 + i * 1000);
      }
      finishRate();
      return Rate::gga.epochs == 15 && Rate::gga.backwards == 0 && contains("utc_epoch_hz=1.000") ? 0 : 102;
    }
    if (scenario == 71) {
      SerialConsole::handleCommand("GPS RATE");
      rateFrame(true, 0); rateFrame(true, 200); rateFrame(true, 100); rateFrame(true, 300); rateFrame(true, 300, false, true);
      finishRate();
      return Rate::gga.epochs == 3 && Rate::gga.backwards == 1 && Rate::gga.duplicates == 1 ? 0 : 103;
    }
    if (scenario == 72) {
      SerialConsole::handleCommand("GPS RATE");
      char longBody[230]; strcpy(longBody, "GNGGA,"); memset(longBody + 6, 'X', 210); longBody[216] = 0;
      nmea(longBody); rateFrame(true, 0);
      finishRate();
      return Rate::overlong == 1 && Rate::gga.sentences == 1 ? 0 : 104;
    }
    return 105;
  }
  if (scenario >= 37 && scenario < 53) {
    const auto advance = [](unsigned count) {
      for (unsigned i = 0; i < count; ++i) { ++mockTime; GpsReceiver::update(); }
    };
    const auto complete = []() {
      for (unsigned i = 0; i < 20000 && GpsUartScan::active(); ++i) { ++mockTime; GpsReceiver::update(); }
    };
    if (scenario == 37 || scenario == 41) {
      if (scenario == 41) mockUarte.CONFIG = UARTE_CONFIG_PARITY_Included << UARTE_CONFIG_PARITY_Pos;
      SerialConsole::handleCommand("GPS SCAN");
      unsigned seeded = 99;
      for (unsigned i = 0; i < 19000 && GpsUartScan::active(); ++i) {
        if (Scan::phase == Scan::Phase::Measure && seeded != Scan::rateIndex) {
          seeded = Scan::rateIndex;
          const uint8_t bytes[] = {0, 0xFF, 'X'};
          Serial1.binary = bytes; Serial1.binaryRemaining = 3;
          mockUarte.ERRORSRC.bits = UARTE_ERRORSRC_FRAMING_Msk | UARTE_ERRORSRC_OVERRUN_Msk | UARTE_ERRORSRC_BREAK_Msk;
          if (scenario == 41) mockUarte.ERRORSRC.bits |= UARTE_ERRORSRC_PARITY_Msk;
          // Consume within this scope so the mock's byte pointer stays alive.
          ++mockTime; GpsReceiver::update();
        }
        ++mockTime; GpsReceiver::update();
      }
      for (const auto& r : Scan::results) {
        if (!r.tested || r.elapsed != 3000 || r.bytes != 3 || r.framing != 1 || r.overrun != 1 || r.breaks != 1 || r.parity != (scenario == 41 ? 1U : 0U)) return 57;
      }
      if (GpsUartScan::active() || GpsUartScan::faulted() || Serial1.writeCalls || SPI.programs || SPI.erases) return 58;
      return contains("restored_38400=YES") && mockUarte.BAUDRATE == UARTE_BAUDRATE_BAUDRATE_Baud38400 && Serial.overfilledWrites == 0 &&
        contains(scenario == 41 ? "parity_flags=1(enabled)" : "parity_flags=0(disabled/8N1)") ? 0 : 59;
    }
    if (scenario == 38) {
      stage = Stage::Record; enabled = true;
      SerialConsole::handleCommand("GPS SCAN");
      stopping = true; SerialConsole::handleCommand("GPS SCAN");
      stage = Stage::Erase; SerialConsole::handleCommand("GPS SCAN");
      return !GpsUartScan::active() && mockUarte.stopCalls == 0 && contains("GPS SCAN refused") ? 0 : 60;
    }
    if (scenario == 39) {
      SerialConsole::handleCommand("GPS SCAN");
      SerialConsole::handleCommand("RECORD START"); SerialConsole::handleCommand("RECORD ERASE");
      SerialConsole::handleCommand("RECORD ERASE CONFIRM"); SerialConsole::handleCommand("ERASE FFF000");
      SerialConsole::handleCommand("RECORD READ"); SerialConsole::handleCommand("DIAG");
      SerialConsole::handleCommand("GPS RAW"); SerialConsole::handleCommand("UART LOOPBACK TEST");
      SerialConsole::handleCommand("GPS SCAN");
      if (enabled || eraseArmed || rawPhase != RawPhase::Idle || loopPhase != LoopPhase::Idle || flashReportRequested) return 61;
      complete();
      return contains("restored_38400=YES") && SPI.programs == 0 && SPI.erases == 0 && Serial1.writeCalls == 0 ? 0 : 62;
    }
    if (scenario == 40) {
      SerialConsole::handleCommand("GPS SCAN");
      unsigned seeded = 99;
      for (unsigned i = 0; i < 19000 && GpsUartScan::active(); ++i) {
        if (Scan::phase == Scan::Phase::Measure && Scan::rateIndex == 3 && seeded != 3) {
          seeded = 3; prepareNmea("GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W");
        }
        ++mockTime; GpsReceiver::update();
      }
      if (Scan::results[3].valid != 1 || gps.passedChecksum() != 0) return 63;
      validFix(true); statusGps();
      return contains("GPS fix=VALID") && mockUarte.PSEL.RXD == 44 && mockUarte.PSEL.TXD == 43 ? 0 : 64;
    }
    if (scenario == 42 || scenario == 43) {
      if (scenario == 42) mockUarte.PSEL.RXD = 2;
      else { mockUarte.ENABLE = 0; mockUarte.EVENTS_RXSTARTED = 0; }
      SerialConsole::handleCommand("GPS SCAN"); advance(30);
      return GpsUartScan::faulted() && mockUarte.stopCalls == 0 && contains("configuration unexpected") &&
        contains(scenario == 42 ? "RXD=0x00000002" : "ENABLE=0") && contains("restored_38400=NO") ? 0 : 65;
    }
    if (scenario == 44) {
      mockUarte.stuckStop = true; SerialConsole::handleCommand("GPS SCAN"); advance(240);
      return contains("RX stop timeout") && contains("restored_38400=NO") && GpsUartScan::faulted() ? 0 : 66;
    }
    if (scenario == 45 || scenario == 46) {
      mockUarte.stuckStart = true; SerialConsole::handleCommand("GPS SCAN");
      for (unsigned i = 0; i < 300; ++i) {
        if (scenario == 45 && Scan::restoring) mockUarte.stuckStart = false;
        ++mockTime; GpsReceiver::update();
      }
      return contains("RX start timeout") && contains(scenario == 45 ? "restored_38400=YES" : "restored_38400=NO") &&
        GpsUartScan::faulted() == (scenario == 46) ? 0 : 67;
    }
    if (scenario == 47) {
      mockTime = 0xFFFFF000; SerialConsole::handleCommand("GPS SCAN"); complete();
      for (const auto& r : Scan::results) if (r.elapsed != 3000) return 68;
      return contains("restored_38400=YES") ? 0 : 69;
    }
    if (scenario == 48) {
      Serial.connected = false; SerialConsole::handleCommand("GPS SCAN"); advance(18100);
      if (!Scan::restored || Scan::phase != Scan::Phase::Report || mockUarte.BAUDRATE != UARTE_BAUDRATE_BAUDRATE_Baud38400) return 70;
      validFix(true);
      if (gps.passedChecksum() != 2) return 71;
      Serial.connected = true; complete();
      return contains("restored_38400=YES") ? 0 : 72;
    }
    if (scenario == 49) {
      stage = Stage::Scan; SerialConsole::handleCommand("GPS SCAN");
      stage = Stage::Idle; gpsTestDiagnosticAvailable = false; SerialConsole::handleCommand("GPS SCAN");
      return !GpsUartScan::active() && mockUarte.stopCalls == 0 ? 0 : 73;
    }
    if (scenario == 50) {
      memset(SPI.memory, 0xFF, sizeof(SPI.memory)); memset(SPI.finalSector, 0xFF, sizeof(SPI.finalSector));
      Serial.connected = false; setup(); startupPending = false; // Explicit-diagnostic regression, boot covered separately.
      for (unsigned i = 0; i < 4100; ++i) { ++mockTime; loop(); }
      const unsigned initialReads = mockImuAxisReads, initialLed = mockRedWrites;
      SerialConsole::handleCommand("GPS SCAN");
      for (unsigned i = 0; i < 18100; ++i) { ++mockTime; loop(); }
      Serial.connected = true; complete();
      return mockImuAxisReads > initialReads + 1000 && mockRedWrites >= initialLed + 18 && !enabled &&
        SPI.programs == 0 && SPI.erases == 0 && Serial1.writeCalls == 0 ? 0 : 74;
    }
    if (scenario == 51) {
      Serial1.loopEcho = true; SerialConsole::handleCommand("UART LOOPBACK TEST"); advance(1051);
      if (Serial1.writeCalls != 1) return 75;
      SerialConsole::handleCommand("GPS SCAN"); complete();
      mockUarte.TXD.AMOUNT = 0;  // Disable may reset DMA metadata; semaphore was idle.
      SerialConsole::handleCommand("UART LOOPBACK TEST"); advance(1051);
      return Serial1.writeCalls == 2 && contains("UART LOOPBACK TEST PASS") && Scan::restored ? 0 : 76;
    }
    if (scenario == 52) {
      const uint8_t bytes[] = {0, 0xFF, 0x01}; Serial1.binary = bytes; Serial1.binaryRemaining = 3;
      mockUarte.ERRORSRC.bits = UARTE_ERRORSRC_FRAMING_Msk;
      SerialConsole::handleCommand("GPS SCAN"); complete();
      for (const auto& r : Scan::results) if (r.bytes || r.framing) return 77;
      return contains("Transition-discarded bytes=3") && Scan::restored ? 0 : 78;
    }
    return 79;
  }
  if (scenario == 53) {
    static uint8_t fullRate[38400]; memset(fullRate, 'X', sizeof(fullRate));
    SerialConsole::handleCommand("GPS RAW"); binaryData(fullRate, sizeof(fullRate)); finishRaw();
    return kRawCapacity >= 38400 + SERIAL_BUFFER_SIZE && rawTotal == 38400 &&
      contains("total_bytes=38400 | stored_bytes=38400 | omitted_bytes=0") &&
      contains("GPS RAW: capturing UART at 38400 baud") && rawPhase == RawPhase::Idle ? 0 : 81;
  }
  if (scenario == 54) {
    memset(SPI.memory, 0xFF, sizeof(SPI.memory)); memset(SPI.finalSector, 0xFF, sizeof(SPI.finalSector));
    Serial.connected = false; setup(); startupPending = false; // Explicit-diagnostic regression, boot covered separately.
    for (unsigned i = 0; i < 4100; ++i) { ++mockTime; loop(); }
    SerialConsole::handleCommand("RECORD START"); SerialConsole::handleCommand("GPS RAW");
    const uint8_t bytes[4] = {0x00, 0x55, 0xAA, 0xFF}; unsigned fraction = 0;
    for (unsigned i = 0; i < 10000; ++i) {
      fraction += 3840;
      Serial1.binary = bytes; Serial1.binaryRemaining = fraction / 1000; fraction %= 1000;
      loop(); ++mockTime;
    }
    SerialConsole::handleCommand("RECORD STOP");
    for (unsigned i = 0; i < 600; ++i) { ++mockTime; loop(); }
    return committed == 1000 && missed == 0 && lost == 0 && rawTotal == 38400 && rawStored == 38400 &&
      SPI.erases == 0 && SPI.violations == 0 && Serial1.writeCalls == 0 ? 0 : 82;
  }
  if (scenario >= 25 && scenario < 37) {
    if (Serial1.writeCalls) return 37;
    const auto loopTicks = [](unsigned count) {
      for (unsigned i = 0; i < count; ++i) { ++mockTime; GpsReceiver::update(); }
    };
    if (scenario == 25) {
      Serial1.loopEcho = true; SerialConsole::handleCommand("UART LOOPBACK TEST"); loopTicks(1051);
      return contains("UART LOOPBACK TEST PASS") && loopSent == 32 && loopReceived == 32 &&
        loopMismatches == 0 && Serial1.writeCalls == 1 && byteCount == 0 && gps.passedChecksum() == 0 && loopPhase == LoopPhase::Idle ? 0 : 38;
    }
    if (scenario == 26) {
      SerialConsole::handleCommand("UART LOOPBACK TEST"); loopTicks(1051);
      if (!contains("UART LOOPBACK TEST FAIL") || loopSent != 32 || loopReceived != 0) return 39;
      Serial1.loopEcho = true; SerialConsole::handleCommand("UART LOOPBACK TEST"); loopTicks(1051);
      return contains("UART LOOPBACK TEST PASS") && Serial1.writeCalls == 2 ? 0 : 40;
    }
    if (scenario == 27) {
      Serial1.loopEcho = Serial1.corruptEcho = true;
      SerialConsole::handleCommand("UART LOOPBACK TEST"); loopTicks(1051);
      return contains("UART LOOPBACK TEST FAIL") && loopReceived == 32 && loopMismatches == 1 ? 0 : 41;
    }
    if (scenario == 28) {
      Serial1.rx = "noise"; SerialConsole::handleCommand("UART LOOPBACK TEST"); loopTicks(51);
      return loopDiscarded == 5 && Serial1.writeCalls == 0 && contains("pretest_discarded=5") && contains("RX not quiet") ? 0 : 42;
    }
    if (scenario == 29) {
      SerialConsole::handleCommand("GPS RAW"); SerialConsole::handleCommand("UART LOOPBACK TEST");
      if (loopPhase != LoopPhase::Idle || !contains("another UART diagnostic")) return 43;
      finishRaw(); Serial1.loopEcho = true; SerialConsole::handleCommand("UART LOOPBACK TEST");
      const uint32_t start = loopStartedAt;
      SerialConsole::handleCommand("UART LOOPBACK TEST"); SerialConsole::handleCommand("GPS RAW");
      SerialConsole::handleCommand("GPS STATUS"); SerialConsole::handleCommand("GPS OFF"); loopTicks(1051);
      return loopSent == 32 && loopReceived == 32 && Serial1.writeCalls == 1 && !printing &&
        contains("wait for its PASS/FAIL") && mockTime - start == 1051 ? 0 : 44;
    }
    if (scenario == 30) {
      Serial1.stuckTx = true; SerialConsole::handleCommand("UART LOOPBACK TEST"); loopTicks(1051);
      if (loopPhase != LoopPhase::Idle) return 45;
      SerialConsole::handleCommand("UART LOOPBACK TEST"); loopTicks(51);
      return Serial1.writeCalls == 1 && contains("previous UART TX not complete") ? 0 : 46;
    }
    if (scenario == 31) {
      Serial.freeSpace = 0; Serial1.loopEcho = true;
      SerialConsole::handleCommand("UART LOOPBACK TEST"); loopTicks(1051);
      if (Serial.used || loopPhase != LoopPhase::Report || loopReceived != 32) return 47;
      Serial.freeSpace = 256; drain();
      return contains("UART LOOPBACK TEST PASS") && Serial.overfilledWrites == 0 ? 0 : 48;
    }
    if (scenario == 32) {
      memset(SPI.memory, 0xFF, sizeof(SPI.memory)); memset(SPI.finalSector, 0xFF, sizeof(SPI.finalSector));
      Serial.connected = false; setup(); startupPending = false; // Explicit-diagnostic regression, boot covered separately.
      for (unsigned i = 0; i < 4100; ++i) { ++mockTime; loop(); }
      SerialConsole::handleCommand("RECORD START"); Serial1.loopEcho = true;
      SerialConsole::handleCommand("UART LOOPBACK TEST");
      for (unsigned i = 0; i < 2000; ++i) { ++mockTime; loop(); }
      SerialConsole::handleCommand("RECORD STOP");
      for (unsigned i = 0; i < 600; ++i) { ++mockTime; loop(); }
      Serial.connected = true; drain();
      return committed == 200 && missed == 0 && lost == 0 && SPI.erases == 0 && SPI.violations == 0 &&
        contains("UART LOOPBACK TEST PASS") && Serial1.writeCalls == 1 ? 0 : 49;
    }
    if (scenario == 33) {
      mockTime = 0xFFFFFFF0; Serial1.loopEcho = true;
      SerialConsole::handleCommand("UART LOOPBACK TEST"); loopTicks(1051);
      return contains("UART LOOPBACK TEST PASS") ? 0 : 50;
    }
    if (scenario == 34) {
      Serial1.shortTx = true; SerialConsole::handleCommand("UART LOOPBACK TEST"); loopTicks(51);
      return contains("UART LOOPBACK TEST FAIL") && loopSent == 31 && contains("short UART write") ? 0 : 51;
    }
    if (scenario == 35) {
      stage = Stage::ExportHeader; SerialConsole::handleCommand("UART LOOPBACK TEST"); loopTicks(1100);
      return loopPhase == LoopPhase::Idle && Serial1.writeCalls == 0 && contains("refused during CSV export") ? 0 : 52;
    }
    if (scenario == 36) {
      Serial1.loopEcho = true; SerialConsole::handleCommand("UART LOOPBACK TEST"); loopTicks(1051);
      if (byteCount || gps.passedChecksum()) return 53;
      validFix(true); statusGps();
      return contains("GPS fix=VALID") && gps.passedChecksum() == 2 && Serial1.baud == 38400 && Serial1.writeCalls == 1 ? 0 : 54;
    }
    return 55;
  }
  if (scenario == 12) {
    SerialConsole::handleCommand("GPS RAW"); drain();
    mockTime = rawStartedAt + 9999; drain();
    if (rawPhase != RawPhase::Capture || contains("GPS RAW END")) return 19;
    finishRaw();
    return contains("total_bytes=0") && contains("valid_nmea=NO") && contains("no UART data") && rawPhase == RawPhase::Idle ? 0 : 20;
  }
  if (scenario == 13) {
    SerialConsole::handleCommand("GPS RAW");
    const uint8_t bytes[] = {0, 1, 0x1B, 0x7F, 0x80, 0xFF, '$', 'A', '\\', '\r', '\n'};
    binaryData(bytes, sizeof(bytes)); finishRaw();
    return rawTotal == sizeof(bytes) && byteCount == sizeof(bytes) &&
      contains("\\x00\\x01\\x1B\\x7F\\x80\\xFF$A\\\\\\x0D\\x0A") &&
      !strchr(Serial.output, '\x1B') && contains("total_bytes=11") ? 0 : 21;
  }
  if (scenario == 14) {
    SerialConsole::handleCommand("GPS RAW"); validFix(true); finishRaw();
    return rawValidSentences == 2 && gps.passedChecksum() == 2 && rawTotal == byteCount &&
      contains("GPS RAW | $GNGGA,123519") && contains("GPS RAW | $GNRMC,123519") &&
      contains("valid_nmea=YES | valid_sentences=2") ? 0 : 22;
  }
  if (scenario == 15) {
    SerialConsole::handleCommand("GPS RAW");
    nmea("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W", false);
    finishRaw();
    return rawTotal > 0 && rawValidSentences == 0 && gps.failedChecksum() == 1 && contains("valid_nmea=NO") ? 0 : 23;
  }
  if (scenario == 16) {
    SerialConsole::handleCommand("GPS RAW");
    mockTime = rawStartedAt + 9999; const uint8_t bytes[] = {0, 2, 3}; binaryData(bytes, 3);
    mockTime = rawStartedAt + 10000; binaryData(bytes, 3); finishRaw();
    return rawTotal == 3 && byteCount == 6 && contains("total_bytes=3") ? 0 : 24;
  }
  if (scenario == 17) {
    SerialConsole::handleCommand("GPS ON"); SerialConsole::handleCommand("GPS RAW");
    const uint32_t start = rawStartedAt; mockTime += 2000;
    SerialConsole::handleCommand("GPS RAW");
    SerialConsole::handleCommand("GPS STATUS");
    SerialConsole::handleCommand("GPS OFF"); validFix(); finishRaw();
    return rawStartedAt == start && rawTotal > 0 && contains("already active") &&
      contains("wait for GPS RAW END") && !printing && rawPhase == RawPhase::Idle ? 0 : 25;
  }
  if (scenario == 18) {
    Serial.freeSpace = 0; SerialConsole::handleCommand("GPS RAW"); validFix();
    mockTime += 10000; drain();
    if (rawPhase == RawPhase::Idle || rawCursor != 0 || Serial.used || gps.passedChecksum() != 2) return 26;
    Serial.freeSpace = 256; drain();
    return rawPhase == RawPhase::Idle && contains("valid_sentences=2") && Serial.overfilledWrites == 0 ? 0 : 27;
  }
  if (scenario == 19) {
    stage = Stage::ExportHeader; SerialConsole::handleCommand("GPS RAW");
    if (rawPhase != RawPhase::Idle || !contains("refused during CSV export")) return 28;
    stage = Stage::Idle; SerialConsole::handleCommand("GPS RAW"); validFix();
    clear(); stage = Stage::ExportHeader; mockTime += 10000; drain();
    if (Serial.used || rawPhase != RawPhase::Dump) return 29;
    stage = Stage::Idle; drain();
    return contains("valid_sentences=2") && rawPhase == RawPhase::Idle ? 0 : 30;
  }
  if (scenario == 20) {
    SerialConsole::handleCommand("GPS RAW"); Serial.connected = false; validFix();
    mockTime += 10000; drain(); Serial.connected = true; drain();
    return contains("valid_sentences=2") && rawPhase == RawPhase::Idle ? 0 : 31;
  }
  if (scenario == 21) {
    static uint8_t large[kRawCapacity + 3]; memset(large, 0xA5, sizeof(large));
    SerialConsole::handleCommand("GPS RAW"); binaryData(large, sizeof(large)); finishRaw();
    return rawTotal == sizeof(large) && rawStored == kRawCapacity && contains("omitted_bytes=3") && rawPhase == RawPhase::Idle ? 0 : 32;
  }
  if (scenario == 22) {
    memset(SPI.memory, 0xFF, sizeof(SPI.memory)); memset(SPI.finalSector, 0xFF, sizeof(SPI.finalSector));
    Serial.connected = false; setup(); startupPending = false; // Explicit-diagnostic regression, boot covered separately.
    for (unsigned i = 0; i < 4100; ++i) { ++mockTime; loop(); }
    SerialConsole::handleCommand("RECORD START"); SerialConsole::handleCommand("GPS RAW");
    for (unsigned i = 0; i < 10000; ++i) {
      if (i % 1000 == 0) prepareNmea("GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W");
      ++mockTime; loop();
    }
    SerialConsole::handleCommand("RECORD STOP");
    for (unsigned i = 0; i < 600; ++i) { ++mockTime; loop(); }
    if (committed != 1000 || missed || lost || rawValidSentences != 10 || rawTotal != byteCount || SPI.erases || SPI.violations) return 33;
    Serial.connected = true; for (unsigned i = 0; i < 50; ++i) GpsReceiver::update();
    return contains("valid_sentences=10") && rawPhase == RawPhase::Idle ? 0 : 34;
  }
  if (scenario == 23) {
    mockTime = 0xFFFFFF00; SerialConsole::handleCommand("GPS RAW"); validFix(); finishRaw();
    return rawValidSentences == 2 && rawPhase == RawPhase::Idle ? 0 : 35;
  }
  if (scenario == 24) {
    prepareNmea("GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W");
    const char* full = Serial1.rx; char prefix[16]; memcpy(prefix, full, 15); prefix[15] = 0;
    Serial1.rx = prefix; drain(); SerialConsole::handleCommand("GPS RAW");
    Serial1.rx = full + 15; while (Serial1.available()) GpsReceiver::update(); finishRaw();
    return gps.passedChecksum() == 1 && rawValidSentences == 0 && contains("valid_nmea=NO") ? 0 : 36;
  }
  if (scenario == 1 || scenario == 2) {
    validFix(scenario == 2); statusGps();
    return contains("UART=Serial1 RX:D7 TX:D6 38400") && contains("NMEA=VALID") && contains("GPS fix=VALID") && contains("lat=48.117300") &&
      contains("lon=11.516667") && contains("satellites=8") && contains("HDOP=0.90") &&
      contains("speed_km/h=41.485") && contains("UTC=12:35:19Z") && contains("last_valid_message_age_ms=0") ? 0 : 4;
  }
  if (scenario == 3) {
    nmea("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", false);
    statusGps();
    return contains("NMEA=NONE") && contains("checksum_fail=1") && contains("no recent checksum-valid NMEA") ? 0 : 5;
  }
  if (scenario == 4) {
    validFix();
    nmea("GNGGA,123520,,,,,0,00,,,,,,,"); statusGps();
    return contains("GPS fix=NO FIX") && contains("position=LAST_KNOWN/UNAVAILABLE") &&
      contains("satellites=0") && contains("HDOP=N/A") && contains("speed_km/h=N/A") ? 0 : 6;
  }
  if (scenario == 5) {
    validFix(); nmea("GNRMC,123520,V,,,,,,,230394,,"); statusGps();
    return contains("GPS fix=NO FIX") && contains("speed_km/h=N/A") ? 0 : 7;
  }
  if (scenario == 6) {
    validFix(); mockTime += 6000; statusGps();
    return contains("NMEA=STALE") && contains("GPS ERROR: UART data stopped") &&
      contains("last_valid_message_age_ms=6000") && contains("GPS fix=STALE/UNKNOWN") ? 0 : 8;
  }
  if (scenario == 7) {
    validFix(); SerialConsole::handleCommand("GPS ON"); clear();
    for (unsigned i = 0; i < 2000; ++i) { ++mockTime; GpsReceiver::update(); }
    drain();
    if (reports() != 2) return 9;
    SerialConsole::handleCommand("GPS OFF"); clear();
    validFix();
    for (unsigned i = 0; i < 3000; ++i) { ++mockTime; GpsReceiver::update(); }
    if (Serial.used || printing || gps.passedChecksum() != 4) return 10;
    return 0;
  }
  if (scenario == 8) {
    Serial.freeSpace = 0; validFix(); statusGps();
    if (Serial.used || gps.passedChecksum() != 2 || Serial.overfilledWrites) return 11;
    Serial.freeSpace = 256; drain();
    return contains("GPS fix=VALID") && Serial.overfilledWrites == 0 ? 0 : 12;
  }
  if (scenario == 9) {
    SerialConsole::handleCommand("GPS ON"); clear(); stage = Stage::ExportHeader;
    validFix(); mockTime += 2000; drain();
    if (Serial.used || gps.passedChecksum() != 2) return 13;
    stage = Stage::Idle; drain();
    return contains("GPS STATUS") ? 0 : 14;
  }
  if (scenario == 10) {
    // Incomplete input must return immediately, then resume when bytes arrive.
    Serial1.rx = "$GNGGA,123"; drain();
    if (!receivedByte || receivedValid) return 15;
    validFix(true); statusGps();
    return contains("NMEA=VALID") && contains("GPS fix=VALID") ? 0 : 16;
  }
  if (scenario == 11) {
    memset(SPI.memory, 0xFF, sizeof(SPI.memory)); memset(SPI.finalSector, 0xFF, sizeof(SPI.finalSector));
    Serial.connected = false; setup(); startupPending = false; // Explicit-diagnostic regression, boot covered separately.
    for (unsigned i = 0; i < 4100; ++i) { ++mockTime; loop(); }
    SerialConsole::handleCommand("RECORD START");
    for (unsigned i = 0; i < 2000; ++i) {
      if (i % 1000 == 0) {
        prepareNmea("GNRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W");
      }
      ++mockTime; loop();
    }
    SerialConsole::handleCommand("RECORD STOP");
    for (unsigned i = 0; i < 600; ++i) { ++mockTime; loop(); }
    return committed == 200 && missed == 0 && lost == 0 && gps.passedChecksum() == 2 &&
      !printing && SPI.erases == 0 && SPI.violations == 0 && sizeof(RecordFormat::Sample) == 16 ? 0 : 17;
  }
  return 18;
}

extern "C" int runCase(unsigned scenario) {
  const int result = runScenario(scenario);
  // Original GPS/RAW paths must remain receive-only unless explicitly testing UART.
  return (scenario < 25 || (scenario >= 37 && scenario < 73)) && Serial1.writeCalls && scenario != 51 ? 56 : result;
}
