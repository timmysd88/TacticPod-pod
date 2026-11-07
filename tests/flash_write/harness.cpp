#include "Arduino.h"
#ifdef TCB_LARGE_TEST_NOR
#include "../recording/SPI.h"
#else
#include "SPI.h"
#endif
uint32_t mockTime = 0;
unsigned mockRedWrites = 0;
unsigned mockImuAxisReads = 0;
MockSerial Serial;
MockSPI SPI;
#include "../../src/gps_receiver.h"
namespace GpsReceiver {
bool mockScanActive=false,mockConfigurationActive=false,mockProfileAuditActive=false;
void begin() {}
void update() {}
bool handleCommand(const char*) { return false; }
bool uartScanBusy() { return mockScanActive || mockConfigurationActive || mockProfileAuditActive; }
bool uartScanActive() { return mockScanActive; }
bool configurationActive() { return mockConfigurationActive; }
bool profileAuditActive() { return mockProfileAuditActive; }
bool hasFreshFix() { return false; }
bool busy() { return false; }
void suspend() {}
void resume() {}
}
// These regressions isolate the original flash test and console/IMU behavior.
// Recording itself is exercised by tests/recording against a larger mock flash.
#include "../../src/recorder.h"
namespace Recorder {
bool mockBusy=false;
const char* mockDiagnosticStage="Idle";
void begin(bool) {}
void update() {}
bool handleCommand(const char*) { return false; }
bool recording() { return false; }
bool exclusive() { return mockBusy; }
const char* diagnosticStage() { return mockDiagnosticStage; }
bool exporting() { return false; }
void addSample(uint32_t, const int16_t[6]) {}
void noteMissed(uint32_t) {}
}
#include "../../src/monotonic_clock.cpp"
#include "../../src/compact_recorder.cpp"
#include "../../src/ram_observations.cpp"
#include "../../src/serial_console.cpp"
// Include the production implementation so no test-only logic replaces it.
#include "../../src/flash_write_test.cpp"
#include "../../src/main.cpp"

#ifndef TCB_LARGE_TEST_NOR
extern "C" int runCase(unsigned scenario) {
  if (scenario == 12) {
    setup(); loop();
    if (SPI.jedecCount != 5) return 26;
    Serial.rx = "DIAG\n"; loop();
    if (SPI.jedecCount != 10 || flashReportRequested) return 27;
    for (unsigned i = 0; i < 1000; ++i) { mockTime += 10; loop(); }
    return SPI.jedecCount == 10 && !SerialConsole::imuPrintingEnabled() && SPI.enableCount == 0 ? 0 : 28;
  }
  if (scenario == 11) {
    setup();
    loop();
    if (!strstr(Serial.output, "TacticPod command menu") || SerialConsole::imuPrintingEnabled()) return 20;
    for (unsigned i = 0; i < 100; ++i) { mockTime += 10; loop(); }
    if (mockImuAxisReads != 60 || strstr(Serial.output, "t_ms=")) return 21;
    Serial.rx = "IMU ON\r\n";
    for (unsigned i = 0; i < 100; ++i) { mockTime += 10; loop(); }
    if (mockImuAxisReads != 120 || !strstr(Serial.output, "t_ms=") || !SerialConsole::imuPrintingEnabled()) return 22;
    Serial.rx = "IMU OFF\n";
    loop();
    Serial.used = 0; Serial.output[0] = 0;
    for (unsigned i = 0; i < 100; ++i) { mockTime += 10; loop(); }
    if (mockImuAxisReads != 180 || strstr(Serial.output, "t_ms=") || SerialConsole::imuPrintingEnabled()) return 23;
    Serial.connected = false;
    for (unsigned i = 0; i < 100; ++i) { mockTime += 10; loop(); }
    if (mockImuAxisReads != 240 || latestImuReading.timestampMs != 4000) return 24;
    if (mockRedWrites != 5 || SPI.enableCount || SPI.eraseCount || SPI.programCount) return 25;
    return 0;
  }
  FlashWriteTest::update();  // Show warning; no confirmation.
  for (unsigned i = 0; i < 100; ++i) { mockTime += 10; FlashWriteTest::update(); }
  if (SPI.enableCount || SPI.eraseCount || SPI.programCount) return 1;
  Serial.rx = "yes\nERASE FFF000 extra\n";
  FlashWriteTest::update();
  if (SPI.enableCount || SPI.eraseCount) return 2;
  if (scenario == 0) return 0;  // Startup/unconfirmed is read-only.
  if (scenario == 2) SPI.wrongId = true;
  if (scenario == 3) SPI.denyWel = true;
  if (scenario == 4) SPI.corruptReadback = true;
  if (scenario == 5) SPI.failedErase = true;
  if (scenario == 6) SPI.stuckBusy = true;
  if (scenario == 9) SPI.stallErase = true;
  if (scenario == 10) SPI.stallProgram = true;
  if (scenario == 7) {
    Serial.rx = "ERASE FFF";
    FlashWriteTest::update();
    Serial.connected = false; FlashWriteTest::update();
    Serial.connected = true; FlashWriteTest::update();
    Serial.rx = "000\n"; FlashWriteTest::update();
    return SPI.enableCount == 0 ? 0 : 3;
  }
  if (scenario == 8) {
    Serial.rx = "ERASE FFF000xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n";
    for (unsigned i = 0; i < 10; ++i) FlashWriteTest::update();
    return SPI.enableCount == 0 ? 0 : 4;
  }
  Serial.rx = "ERASE FFF000\r\nERASE FFF000\n";
  FlashWriteTest::update();
  for (unsigned i = 0; i < 900; ++i) { mockTime += 10; FlashWriteTest::update(); }
  if (SPI.violations) return 5;
  if (SPI.jedecCount != 5) return 6;
  if (scenario == 2) return SPI.enableCount == 0 && strstr(Serial.output, "FLASH TEST FAIL") ? 0 : 7;
  if (scenario == 3 || scenario == 6) return SPI.eraseCount == 0 && !FlashWriteTest::diagnosticsAllowed() ? 0 : 8;
  if (SPI.eraseCount != 1) return 9;
  if (scenario == 9) return SPI.programCount == 0 && !FlashWriteTest::diagnosticsAllowed() && strstr(Serial.output, "timed out") ? 0 : 18;
  if (scenario == 5) return SPI.programCount == 0 && strstr(Serial.output, "sector did not erase") ? 0 : 10;
  if (SPI.programCount != 1) return 11;
  if (scenario == 10) return !FlashWriteTest::diagnosticsAllowed() && strstr(Serial.output, "timed out") ? 0 : 19;
  if (scenario == 4) return strstr(Serial.output, "Mismatch at 0xFFF000") && strstr(Serial.output, "Mismatch at 0xFFF0FF") && strstr(Serial.output, "mismatches=2") ? 0 : 12;
  if (!strstr(Serial.output, "FLASH TEST PASS")) return 13;
  for (unsigned i = 0; i < 256; ++i) if (SPI.memory[i] != (uint8_t(i) ^ 0xA5)) return 14;
  for (unsigned i = 256; i < 4096; ++i) if (SPI.memory[i] != 0xFF) return 15;
  // Advancing time or reconnecting must not schedule another destructive test.
  Serial.connected = false; FlashWriteTest::update();
  Serial.connected = true; FlashWriteTest::update();
  for (unsigned i = 0; i < 100; ++i) { mockTime += 10; FlashWriteTest::update(); }
  if (SPI.eraseCount != 1 || SPI.programCount != 1) return 16;
  Serial.rx = "ERASE FFF000\n"; FlashWriteTest::update();
  for (unsigned i = 0; i < 100; ++i) { mockTime += 10; FlashWriteTest::update(); }
  return SPI.eraseCount == 2 && SPI.programCount == 2 ? 0 : 17;
}
#endif
