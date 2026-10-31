#include "Arduino.h"
#include "SPI.h"
#include "../../src/flash_write_test.h"
uint32_t mockTime = 0;
unsigned mockRedWrites = 0, mockImuAxisReads = 0;
MockSerial Serial;
MockSPI SPI;
#include "../../src/gps_receiver.h"
namespace GpsReceiver {
void begin() {}
void update() {}
bool handleCommand(const char*) { return false; }
bool uartScanBusy() { return false; }
}
bool diagnosticAvailable = true;
namespace FlashWriteTest {
bool diagnosticsAllowed() { return diagnosticAvailable; }
void update() {}
}
#include "../../src/recorder.cpp"
#include "../../src/monotonic_clock.cpp"
#include "../../src/compact_recorder.cpp"
#include "../../src/ram_observations.cpp"
#include "../../src/serial_console.cpp"
#include "../../src/main.cpp"

void tick(unsigned count = 1) {
  for (unsigned i = 0; i < count; ++i) { ++mockTime; Recorder::update(); }
}
void rebootRecorder() {
  // Real reset clears RAM but preserves the flash array.
  stage = Stage::Idle; io = IO::None; qCount = pageSamples = 0;
  eraseArmed = false; committed = 0; lost = missed = 0;
  Recorder::begin(true); tick(4100);
}
void feed(unsigned count) {
  const int16_t axes[6] = {1, -2, 16384, -100, 200, -300};
  for (unsigned i = 0; i < count; ++i) { Recorder::addSample(mockTime, axes); tick(10); }
}
void stop() { Recorder::handleCommand("RECORD STOP"); tick(500); }
bool validAt(unsigned at) { return RecordFormat::valid(SPI.memory + at); }

extern "C" int runCase(unsigned scenario) {
  memset(SPI.memory, 0xFF, sizeof(SPI.memory)); memset(SPI.finalSector, 0xFF, sizeof(SPI.finalSector));
  rebootRecorder();
  if (Recorder::exclusive() || SPI.enables || SPI.erases || SPI.programs) return 1;
  if (scenario == 0) {
    Recorder::handleCommand("RECORD ERASE CONFIRM"); tick(10);
    return SPI.erases == 0 && !Recorder::recording() ? 0 : 2;
  }
  if (scenario == 1) {
    Recorder::handleCommand("RECORD START"); feed(205); stop();
    if (committed != 205 || SPI.erases || SPI.violations || Recorder::exclusive()) return 3;
    if (!validAt(0) || !validAt(256) || !validAt(4096) || !validAt(4096 + 15 * 256)) return 4;
    if (get32(SPI.memory + 256 + 12) != 14 || get32(SPI.memory + 4096 + 256 + 12) != 9) return 5;
    if (get32(SPI.memory + 272) != 4100) return 31;
    const int16_t expectedAxes[6] = {1, -2, 16384, -100, 200, -300};
    for (unsigned axis = 0; axis < 6; ++axis) {
      const uint8_t* p = SPI.memory + 276 + axis * 2;
      if (int16_t(uint16_t(p[0]) | uint16_t(p[1]) << 8) != expectedAxes[axis]) return 32;
    }
    uint8_t saved[256]; memcpy(saved, SPI.memory, 256);
    rebootRecorder();
    if (nextSector != 8192 || Recorder::recording() || SPI.erases) return 6;
    Recorder::handleCommand("RECORD START"); feed(3); stop();
    if (memcmp(saved, SPI.memory, 256) || committed != 3 || !validAt(8192)) return 7;
    Serial.used = 0; Serial.output[0] = 0;
    Recorder::handleCommand("RECORD READ"); tick(1000);
    unsigned rows = 0; for (const char* p = Serial.output; *p; ++p) if (*p == '\n') ++rows;
    return rows == 211 && strstr(Serial.output, "END_RECORD_CSV") && SPI.violations == 0 ? 0 : 8;
  }
  if (scenario == 2) {
    Recorder::handleCommand("RECORD START"); feed(28); tick(100);
    // Tear the second data page's marker, then reset without STOP/footer.
    memset(SPI.memory + 512 + 252, 0xFF, 4);
    unsigned programs = SPI.programs;
    rebootRecorder();
    Serial.used = 0; Serial.output[0] = 0;
    Recorder::handleCommand("RECORD READ"); tick(300);
    unsigned rows = 0; for (const char* p = Serial.output; *p; ++p) if (*p == '\n') ++rows;
    if (rows != 17 || SPI.programs != programs || nextSector != 4096) return 9;
    Recorder::handleCommand("RECORD START"); feed(1); stop();
    return validAt(4096) && SPI.erases == 0 ? 0 : 10;
  }
  if (scenario == 3) {
    // Torn header reserves the sector even with no valid marker.
    SPI.memory[0] = 0; rebootRecorder();
    if (nextSector != 4096) return 11;
    Recorder::handleCommand("RECORD START"); feed(1); stop();
    return SPI.memory[0] == 0 && validAt(4096) ? 0 : 12;
  }
  if (scenario == 4) {
    // Interrupted erase may leave a blank header above old data: never overwrite it.
    SPI.memory[300] = 0x12;
    Recorder::handleCommand("RECORD START"); tick(100);
    return stage == Stage::Fault && SPI.programs == 0 && SPI.erases == 0 && SPI.memory[300] == 0x12 ? 0 : 13;
  }
  if (scenario == 5) {
    SPI.denyWel = true; Recorder::handleCommand("RECORD START"); tick(100);
    return stage == Stage::Fault && SPI.programs == 0 ? 0 : 14;
  }
  if (scenario == 6) {
    Recorder::handleCommand("RECORD START"); tick(17);
    SPI.stuckBusy = true; tick(1100);
    return stage == Stage::Fault && !Recorder::recording() ? 0 : 15;
  }
  if (scenario == 7) {
    Recorder::handleCommand("RECORD START");
    const int16_t axes[6] = {};
    for (unsigned i = 0; i < 257; ++i) Recorder::addSample(i * 10, axes);
    tick(2000);
    return lost == 1 && committed == 256 && !Recorder::recording() && stage == Stage::Idle ? 0 : 16;
  }
  if (scenario == 8) {
    nextSector = 0xFFE000;
    Recorder::handleCommand("RECORD START"); feed(210); tick(500);
    return stage == Stage::Idle && committed == 196 && lost == 14 && nextSector == 0xFFF000 && SPI.violations == 0 ? 0 : 17;
  }
  if (scenario == 9) {
    SPI.memory[0] = 0;
    Recorder::handleCommand("RECORD ERASE"); tick(30001);
    Recorder::handleCommand("RECORD ERASE CONFIRM"); tick(10);
    if (SPI.erases) return 18;
    Recorder::handleCommand("RECORD ERASE"); Recorder::handleCommand("RECORD ERASE CONFIRM"); tick(200);
    return SPI.erases > 0 && SPI.memory[0] == 0xFF && !Recorder::recording() && SPI.violations == 0 ? 0 : 19;
  }
  if (scenario == 10) {
    diagnosticAvailable = false;
    Recorder::handleCommand("RECORD START"); tick(100);
    return SPI.programs == 0 && !Recorder::recording() ? 0 : 20;
  }
  if (scenario == 11) {
    SPI.corruptProgram = true; Recorder::handleCommand("RECORD START"); tick(100);
    return stage == Stage::Fault && !validAt(0) ? 0 : 21;
  }
  if (scenario == 12) {
    SPI.wrongId = true; Recorder::handleCommand("RECORD START"); tick(100);
    return stage == Stage::Fault && SPI.programs == 0 && SPI.erases == 0 ? 0 : 22;
  }
  if (scenario == 13) {
    // A session stopping with no samples still gets a valid header/footer.
    Recorder::handleCommand("RECORD START"); stop();
    return stage == Stage::Idle && committed == 0 && validAt(0) && validAt(3840) ? 0 : 23;
  }
  if (scenario == 14) {
    Serial.connected = false;
    setup();
    for (unsigned i = 0; i < 4100; ++i) { ++mockTime; loop(); }
    if (stage != Stage::Idle) return 25;
    if (!SerialConsole::handleCommand("RECORD START")) return 26;
    for (unsigned i = 0; i < 2000; ++i) { ++mockTime; loop(); }
    SerialConsole::handleCommand("RECORD STOP");
    for (unsigned i = 0; i < 600; ++i) { ++mockTime; loop(); }
    return committed == 200 && missed == 0 && lost == 0 && SPI.erases == 0 && stage == Stage::Idle ? 0 : 27;
  }
  if (scenario == 15) {
    SPI.finalSector[0] = 0;
    Recorder::handleCommand("RECORD ERASE"); Recorder::handleCommand("RECORD ERASE CONFIRM");
    sector = 0xFFE000;  // Exercise the last iteration of the bounded erase loop.
    tick(200);
    return stage == Stage::Idle && nextSector == 0 && SPI.erases == 1 && SPI.violations == 0 && !enabled ? 0 : 28;
  }
  if (scenario == 16) {
    Recorder::handleCommand("RECORD START"); feed(28); stop();
    SPI.memory[256 + 20] ^= 1;  // Corrupt payload while leaving the commit marker intact.
    rebootRecorder();
    Serial.used = 0; Serial.output[0] = 0;
    Recorder::handleCommand("RECORD READ"); tick(100);
    unsigned rows = 0; for (const char* p = Serial.output; *p; ++p) if (*p == '\n') ++rows;
    return rows == 3 ? 0 : 29;  // BEGIN, column header, END; CRC rejects corrupted data.
  }
  if (scenario == 17) {
    Recorder::handleCommand("RECORD ERASE"); Recorder::handleCommand("RECORD ERASE CONFIRM");
    tick(); SPI.stuckBusy = true; tick(6000);
    return stage == Stage::Fault && SPI.erases == 1 ? 0 : 30;
  }
  if (scenario == 18) {
    Serial.connected = false;
    setup();
    for (unsigned i = 0; i < 4100; ++i) { ++mockTime; loop(); }
    if (!scanNoticePending || flashReportPrinted || Recorder::recording()) return 33;
    Serial.connected = true;
    Serial.used = 0; Serial.output[0] = 0;
    loop();
    if (!strstr(Serial.output, "RECORD scan complete") || !strstr(Serial.output, "FLASH JEDEC read 5/5") || !flashReportPrinted) return 34;
    const size_t startupLength = Serial.used;
    for (unsigned i = 0; i < 10000; ++i) { ++mockTime; loop(); }
    if (Serial.used != startupLength) return 35;
    Serial.connected = false; loop(); Serial.connected = true; loop();
    if (Serial.used != startupLength) return 36;
    if (!SerialConsole::handleCommand("DIAG")) return 37;
    loop();
    if (Serial.used <= startupLength || flashReportRequested) return 38;
    const size_t diagnosticLength = Serial.used;
    for (unsigned i = 0; i < 5000; ++i) { ++mockTime; loop(); }
    return Serial.used == diagnosticLength && SPI.enables == 0 && SPI.violations == 0 ? 0 : 39;
  }
  if (scenario == 19) {
    Recorder::handleCommand("RECORD START");
    SerialConsole::handleCommand("DIAG");
    if (flashReportRequested || !strstr(Serial.output, "DIAG refused")) return 40;
    stage = Stage::ExportHeader; SerialConsole::handleCommand("DIAG");
    if (flashReportRequested) return 41;
    stage = Stage::Fault; SerialConsole::handleCommand("DIAG");
    if (flashReportRequested) return 42;
    stage = Stage::Idle; diagnosticAvailable = false; SerialConsole::handleCommand("DIAG");
    return !flashReportRequested && SPI.enables == 0 ? 0 : 43;
  }
  return 24;
}
