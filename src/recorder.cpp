#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <SPI.h>
#include <string.h>
#include "recorder.h"
#include "recorder_format.h"
#include "flash_write_test.h"
#include "tcb3_session.h"
#include "tcb3_test_limits.h"

namespace {
using namespace RecordFormat;
constexpr unsigned kQueueSize = 256;
enum class Stage { Scan, Idle, CheckBlank, Header, Record, Footer, Erase, EraseVerify, ExportHeader, ExportPage, Fault };
enum class IO { None, Body, Marker, Erase };
Stage stage = Stage::Idle;
IO io = IO::None;
bool enabled = false, imuAvailable = false, stopping = false, eraseArmed = false;
bool identified = false;
bool scanNoticePending = false;
// Reusable builds only: sector 0 shows a reset marker or an interrupted reset, so V1 sessions must stay hidden.
bool exportHidden = false, sector0Blank = false, lowData = false;
uint32_t sector0Magic = 0;
uint32_t scanAt = 0, nextSector = 0, sector = 0, session = 0, sectorSequence = 0;
uint32_t startMs = 0, committed = 0, lost = 0, missed = 0, pageSequence = 0;
uint32_t ioAt = 0, ioStarted = 0, lastPoll = 0, eraseArmedAt = 0;
uint32_t exportAt = 0, exportSession = 0, exportSequence = 0;
unsigned checkOffset = 0, pageIndex = 1, pageSamples = 0;
unsigned qHead = 0, qTail = 0, qCount = 0, exportPage = 1, exportSample = 0;
Sample queue[kQueueSize];
uint8_t buffer[256], scratch[256], exportBuffer[256];

void select() {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(2, LOW);
}
void deselect() { digitalWrite(2, HIGH); SPI.endTransaction(); }
void address(uint32_t value) {
  SPI.transfer(uint8_t(value >> 16)); SPI.transfer(uint8_t(value >> 8)); SPI.transfer(uint8_t(value));
}
uint8_t status() {
  select(); SPI.transfer(0x05); uint8_t value = SPI.transfer(0); deselect(); return value;
}
void read(uint32_t at, uint8_t* output, size_t length) {
  select(); SPI.transfer(0x03); address(at);
  for (size_t i = 0; i < length; ++i) output[i] = SPI.transfer(0);
  deselect();
}
void fault(const char* why) {
  Serial.print("RECORD ERROR: "); Serial.println(why);
  Serial.print("Committed samples="); Serial.print(committed);
  Serial.print("; uncommitted RAM samples="); Serial.println(qCount + pageSamples);
  Serial.println("Recording stopped. Flash commands locked until reboot; committed pages remain recoverable.");
  enabled = false; stopping = false; io = IO::None; stage = Stage::Fault; eraseArmed = false;
}
bool identity() {
  for (unsigned i = 0; i < 5; ++i) {
    select(); SPI.transfer(0x9F);
    uint8_t a = SPI.transfer(0), b = SPI.transfer(0), c = SPI.transfer(0); deselect();
    if (a != 0x0B || b != 0x40 || c != 0x18) return false;
  }
  return true;
}
bool wren() {
  uint8_t s = status();
  if (s == 0xFF || (s & 1)) { fault("flash busy/unresponsive before WREN"); return false; }
  select(); SPI.transfer(0x06); deselect();
  s = status();
  if (s == 0xFF || (s & 3) != 2) { fault("write-enable latch did not set"); return false; }
  return true;
}
bool program(uint32_t at, const uint8_t* data, size_t length) {
  if (!length || at >= kEnd || length > kEnd - at || (at & 255) + length > 256) {
    fault("program address/page boundary rejected"); return false;
  }
  if (!wren()) return false;
  select(); SPI.transfer(0x02); address(at);
  for (size_t i = 0; i < length; ++i) SPI.transfer(data[i]);
  deselect(); ioStarted = millis(); lastPoll = ioStarted; return true;
}
void page(uint32_t magic, uint32_t count) {
  memset(buffer, 0xFF, sizeof(buffer));
  put32(buffer, magic); put32(buffer + 4, session);
  put32(buffer + 8, pageSequence); put32(buffer + 12, count);
}
void writePage(uint32_t at) {
  put32(buffer + 248, crc(buffer));
  put32(buffer + 252, 0xFFFFFFFFUL);
  // Never program onto an existing or partially written page.
  read(at, scratch, sizeof(scratch));
  if (!blank(scratch, sizeof(scratch))) { fault("destination page is not blank; existing data preserved"); return; }
  if (program(at, buffer, sizeof(buffer))) { ioAt = at; io = IO::Body; }
}
void statusReport() {
  Serial.print("RECORD STATUS: ");
  Serial.print(stage == Stage::Fault ? "FAULT" : stage == Stage::Scan ? "SCANNING" :
      enabled ? (stopping ? "STOPPING" : "RECORDING") :
      stage == Stage::Erase || stage == Stage::EraseVerify ? "ERASING" :
      stage == Stage::ExportHeader || stage == Stage::ExportPage ? "EXPORTING" : "STOPPED");
  Serial.print(" session="); Serial.print(session);
  Serial.print(" committed_samples="); Serial.print(committed);
  Serial.print(" buffered_samples="); Serial.print(qCount + pageSamples);
  Serial.print(" dropped_samples="); Serial.print(lost);
  Serial.print(" missed_deadlines="); Serial.print(missed);
  Serial.print(" next_sector=0x"); Serial.println(nextSector, HEX);
}
void finishProgram() {
  if (stage == Stage::Header) { pageIndex = 1; stage = Stage::Record; }
  else if (stage == Stage::Record) { committed += pageSamples; pageSamples = 0; ++pageIndex; ++pageSequence; }
  else if (stage == Stage::Footer) {
    enabled = false; stopping = false; stage = Stage::Idle;
    Serial.println("RECORD STOPPED: partial page flushed and session footer committed."); statusReport();
  }
}
void serviceIO() {
  const uint32_t now = millis();
  if (uint32_t(now - lastPoll) < 2) return;
  lastPoll = now;
  const uint8_t s = status();
  if (s == 0xFF) { fault("status returned 0xFF"); return; }
  if (s & 1) {
    if (uint32_t(now - ioStarted) >= (io == IO::Erase ? 5000UL : 1000UL)) fault("flash busy timeout");
    return;
  }
  if (s & 2) { fault("command rejected or WEL did not clear"); return; }
  if (io == IO::Erase) { io = IO::None; checkOffset = 0; stage = Stage::EraseVerify; return; }
  read(ioAt, scratch, sizeof(scratch));
  if (memcmp(buffer, scratch, sizeof(buffer)) != 0) { fault("page readback verification failed"); return; }
  if (io == IO::Body) {
    put32(buffer + 252, kCommit);
    if (program(ioAt + 252, buffer + 252, 4)) io = IO::Marker;
  } else {
    if (!valid(scratch)) { fault("committed page CRC/marker failed"); return; }
    io = IO::None; finishProgram();
  }
}
void csvSample(const uint8_t* p) {
  Serial.print(exportSession); Serial.print(','); Serial.print(get32(p));
  for (unsigned i = 0; i < 6; ++i) {
    int16_t value = int16_t(uint16_t(p[4 + i * 2]) | uint16_t(p[5 + i * 2]) << 8);
    Serial.print(','); Serial.print(float(value) * (i < 3 ? 0.000061f : 0.00875f), 6);
  }
  Serial.println();
}
}

namespace Recorder {
void begin(bool ready) {
  imuAvailable = ready; enabled = false; identified = false;
  scanNoticePending = false; exportHidden = false; sector0Blank = false; lowData = false; sector0Magic = 0;
  scanAt = 0; nextSector = 0; stage = Stage::Scan; io = IO::None;
  ioStarted = millis();
}
bool recording() { return enabled && !stopping; }
bool exporting() { return stage == Stage::ExportHeader || stage == Stage::ExportPage || Tcb3Session::exporting(); }
bool exclusive() { return stage != Stage::Idle; }
void noteMissed(uint32_t count) { if (enabled && !stopping) missed += count; }
void addSample(uint32_t ms, const int16_t axes[6]) {
  if (!recording()) return;
  if (qCount == kQueueSize) {
    ++lost; stopping = true;
    Serial.println("RECORD ERROR: RAM buffer overflow; stopping and flushing retained samples."); return;
  }
  queue[qTail].ms = ms; memcpy(queue[qTail].axes, axes, 12);
  qTail = (qTail + 1) % kQueueSize; ++qCount;
}
bool handleCommand(const char* line) {
  if (strcmp(line, "RECORD STATUS") == 0) { statusReport(); return true; }
  if (strcmp(line, "RECORD STOP") == 0) {
    if (enabled) { stopping = true; Serial.println("RECORD STOP requested; flushing queued samples."); }
    else Serial.println("RECORD is not running.");
    return true;
  }
  const bool start = strcmp(line, "RECORD START") == 0;
  const bool dump = strcmp(line, "RECORD READ") == 0;
  const bool erase = strcmp(line, "RECORD ERASE") == 0;
  const bool confirm = strcmp(line, "RECORD ERASE CONFIRM") == 0;
  if (!start && !dump && !erase && !confirm) return false;
  if(Tcb3Test::Guarded && (start || erase || confirm)){
    Serial.println("RECORD mutation refused: bounded TCB3 test build.");return true;
  }
  if (stage != Stage::Idle || !FlashWriteTest::diagnosticsAllowed()) {
    Serial.println("RECORD command refused: scanning/busy/faulted; use RECORD STATUS."); return true;
  }
  if (erase) {
    eraseArmed = true; eraseArmedAt = millis();
    Serial.println("WARNING: erase destroys ALL recordings at 0x000000-0xFFEFFF. Diagnostic sector is preserved.");
    Serial.println("Within 30 seconds send RECORD ERASE CONFIRM to authorise; no erase has started."); return true;
  }
  if (confirm) {
    const bool authorised = eraseArmed && uint32_t(millis() - eraseArmedAt) <= 30000;
    eraseArmed = false;
    if (!authorised) { Serial.println("Erase refused: first send RECORD ERASE and read its warning."); return true; }
    if (!identity()) { fault("JEDEC changed before confirmed erase"); return true; }
    sector = 0; stage = Stage::Erase; Serial.println("Confirmed recording-area sector erase started."); return true;
  }
  eraseArmed = false;
  if (dump) {
    exportAt = 0; stage = exportHidden ? Stage::Idle : Stage::ExportHeader;
    Serial.println("BEGIN_RECORD_CSV");
    Serial.println("session,timestamp_ms,accel_x_g,accel_y_g,accel_z_g,gyro_x_dps,gyro_y_dps,gyro_z_dps");
    if (exportHidden) Serial.println("END_RECORD_CSV");
    return true;
  }
  if (!imuAvailable) { Serial.println("RECORD START refused: IMU initialisation failed."); return true; }
  if (nextSector >= kEnd) { Serial.println("Recording area full. Export first; overwrite requires RECORD ERASE then RECORD ERASE CONFIRM."); return true; }
  if (!identity()) { fault("JEDEC changed before recording"); return true; }
  sector = nextSector; session = sector / kSectorSize + 1; sectorSequence = 0;
  startMs = millis(); committed = lost = missed = pageSequence = 0;
  qHead = qTail = qCount = pageSamples = 0; checkOffset = 0;
  stopping = false; enabled = true; stage = Stage::CheckBlank;
  Serial.println("RECORD START: 100 Hz, append-only; occupied sectors will never be overwritten."); return true;
}
void update() {
  // Keep startup completion visible even when scanning finished before USB opened.
  // This is a notification only; it never schedules any flash operation.
  if (scanNoticePending && Serial && !exporting()) {
    Serial.println("RECORD scan complete. Recording OFF at startup; append position recovered without writes.");
    scanNoticePending = false;
  }
  if (!FlashWriteTest::diagnosticsAllowed() || stage == Stage::Idle || stage == Stage::Fault) return;
  if (io != IO::None) { serviceIO(); return; }
  if (stage == Stage::Scan) {
    if (!identified) {
      const uint8_t s = status();
      if (s == 0xFF) { fault("flash unavailable during startup scan"); return; }
      if (s & 1) { if (uint32_t(millis() - ioStarted) >= 5000) fault("startup busy timeout"); return; }
      if (!identity()) { fault("expected JEDEC 0B 40 18 not found"); return; }
      identified = true;
    }
    // A nonblank header reserves the entire sector, even if its CRC is torn.
    // Blank headers are not proof of free space: check all 4 KB before writing.
    read(scanAt, scratch, 16);
    if (scanAt == 0) { sector0Magic = get32(scratch); sector0Blank = blank(scratch, 16); }
    if (!blank(scratch, 16)) { nextSector = scanAt + kSectorSize; if (scanAt > 0 && scanAt < Tcb3Test::WriteStart) lowData = true; }
    scanAt += kSectorSize;
    if (scanAt >= kEnd) {
      stage = Stage::Idle;
      scanNoticePending = true;
      exportHidden = Tcb3Test::Reusable &&
        (sector0Magic == 0x33525054UL ||
         (!sector0Blank && sector0Magic != kHeader && sector0Magic != 0x32485054UL && sector0Magic != 0x33485054UL) ||
         (sector0Blank && lowData));
    }
  } else if (stage == Stage::CheckBlank) {
    read(sector + checkOffset, scratch, sizeof(scratch));
    if (!blank(scratch, sizeof(scratch))) {
      fault("candidate sector contains data; refusing overwrite (explicit erase required)"); return;
    }
    checkOffset += sizeof(scratch);
    if (checkOffset == kSectorSize) {
      // Advance before any write: a failed/torn header consumes this sector.
      nextSector = sector + kSectorSize;
      page(kHeader, sectorSequence); put32(buffer + 16, 1); put32(buffer + 20, 100);
      put32(buffer + 24, 61); put32(buffer + 28, 8750); put32(buffer + 32, startMs);
      stage = Stage::Header; writePage(sector);
    }
  } else if (stage == Stage::Record) {
    if (stopping && qCount == 0) {
      page(kFooter, committed); put32(buffer + 16, lost); put32(buffer + 20, missed);
      stage = Stage::Footer; writePage(sector + 15 * kPageSize); return;
    }
    if (qCount < kSamplesPerPage && !stopping) return;
    if (pageIndex == 15) {
      if (nextSector >= kEnd) {
        lost += qCount; qCount = 0; stopping = true;
        Serial.println("RECORD area full: queued tail cannot be stored; closing session. No overwrite."); return;
      }
      sector = nextSector; ++sectorSequence; checkOffset = 0; stage = Stage::CheckBlank; return;
    }
    pageSamples = qCount < kSamplesPerPage ? qCount : kSamplesPerPage;
    page(kData, pageSamples);
    for (unsigned i = 0; i < pageSamples; ++i) {
      uint8_t* p = buffer + 16 + i * 16;
      const Sample& sample = queue[qHead]; put32(p, sample.ms);
      for (unsigned axis = 0; axis < 6; ++axis) {
        const uint16_t value = uint16_t(sample.axes[axis]); p[4 + axis * 2] = value; p[5 + axis * 2] = value >> 8;
      }
      qHead = (qHead + 1) % kQueueSize; --qCount;
    }
    writePage(sector + pageIndex * kPageSize);
  } else if (stage == Stage::Erase) {
    if (sector >= kEnd || (sector & (kSectorSize - 1))) { fault("erase boundary rejected"); return; }
    if (!wren()) return;
    select(); SPI.transfer(0x20); address(sector); deselect();
    ioStarted = millis(); lastPoll = ioStarted; io = IO::Erase;
  } else if (stage == Stage::EraseVerify) {
    read(sector + checkOffset, scratch, sizeof(scratch));
    if (!blank(scratch, sizeof(scratch))) { fault("confirmed erase verification failed"); return; }
    checkOffset += sizeof(scratch);
    if (checkOffset == kSectorSize) {
      sector += kSectorSize;
      if (sector == kEnd) {
        nextSector = 0; stage = Stage::Idle;
        Serial.println("RECORD erase complete; diagnostic sector untouched. Recording remains OFF.");
      } else stage = Stage::Erase;
    }
  } else if (stage == Stage::ExportHeader) {
    if (!Serial) { stage = Stage::Idle; return; }
    if (exportAt >= nextSector) { stage = Stage::Idle; Serial.println("END_RECORD_CSV"); return; }
    read(exportAt, scratch, sizeof(scratch));
    if (valid(scratch) && get32(scratch) == kHeader && get32(scratch + 16) == 1 &&
        get32(scratch + 20) == 100 && get32(scratch + 24) == 61 && get32(scratch + 28) == 8750) {
      exportSession = get32(scratch + 4); exportPage = 1; exportSample = 0;
      exportSequence = get32(scratch + 8); stage = Stage::ExportPage;
    } else exportAt += kSectorSize;
  } else if (stage == Stage::ExportPage) {
    if (!Serial) { stage = Stage::Idle; return; }
    if (exportPage == 15) { exportAt += kSectorSize; stage = Stage::ExportHeader; return; }
    if (exportSample == 0) {
      read(exportAt + exportPage * kPageSize, exportBuffer, sizeof(exportBuffer));
      const uint32_t count = get32(exportBuffer + 12);
      if (!valid(exportBuffer) || get32(exportBuffer) != kData ||
          get32(exportBuffer + 4) != exportSession || get32(exportBuffer + 8) != exportSequence ||
          count == 0 || count > kSamplesPerPage) {
        // Stop this sector at its first invalid/torn page; earlier commits survive.
        exportPage = 15; return;
      }
    }
    csvSample(exportBuffer + 16 + exportSample * 16);
    if (++exportSample == get32(exportBuffer + 12)) { exportSample = 0; ++exportPage; ++exportSequence; }
  }
}
}
