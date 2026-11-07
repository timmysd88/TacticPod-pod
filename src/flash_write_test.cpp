#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <SPI.h>
#include <string.h>
#include "flash_write_test.h"
#include "serial_console.h"
#include "recorder.h"
#include "tcb3_session.h"
#include "tcb3_test_limits.h"

namespace {
constexpr uint8_t kCs = 2;
constexpr uint32_t kSector = 0xFFF000UL;
constexpr uint32_t kSectorSize = 4096;
constexpr uint32_t kCapacity = 0x1000000UL;
constexpr size_t kPageSize = 256;
constexpr unsigned long kReadyTimeoutMs = 1000;
constexpr unsigned long kEraseTimeoutMs = 5000;
constexpr unsigned long kProgramTimeoutMs = 1000;
constexpr unsigned long kPollIntervalMs = 2;
constexpr char kConfirmation[] = "ERASE FFF000";
static_assert(kSector + kSectorSize == kCapacity, "Use only the final sector");
static_assert(kSector % kSectorSize == 0, "Sector must be aligned");
static_assert(kSector % kPageSize == 0 && kPageSize <= kSectorSize,
              "Pattern must fit one aligned page");

enum class State { Idle, Ready, Erasing, VerifyErase, Programming, Compare, Fault };
State state = State::Idle;
unsigned long operationStartedMs = 0;
unsigned long lastPollMs = 0;
uint32_t eraseOffset = 0;
size_t compareOffset = 0;
uint16_t mismatchCount = 0;
uint8_t readback[kPageSize];
char input[40];
size_t inputLength = 0;
bool inputOverflow = false;
bool wasConnected = false;

uint8_t pattern(size_t offset) {
  return static_cast<uint8_t>(offset) ^ 0xA5;
}

void selectFlash() {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(kCs, LOW);
}

void deselectFlash() {
  digitalWrite(kCs, HIGH);
  SPI.endTransaction();
}

void sendAddress(uint32_t address) {
  SPI.transfer(static_cast<uint8_t>(address >> 16));
  SPI.transfer(static_cast<uint8_t>(address >> 8));
  SPI.transfer(static_cast<uint8_t>(address));
}

uint8_t readStatus() {
  selectFlash();
  SPI.transfer(0x05);
  const uint8_t result = SPI.transfer(0x00);
  deselectFlash();
  return result;
}

void hexAddress(uint32_t address) {
  Serial.print("0x");
  for (int shift = 20; shift >= 0; shift -= 4) {
    Serial.print((address >> shift) & 0xF, HEX);
  }
}

void hexByte(uint8_t value) {
  Serial.print("0x");
  if (value < 16) Serial.print('0');
  Serial.print(value, HEX);
}

void fail(const char* reason, bool uncertainBusy = false) {
  Serial.print("FLASH TEST FAIL: ");
  Serial.println(reason);
  // After a timeout, do not send further commands into an uncertain busy chip.
  state = uncertainBusy ? State::Fault : State::Idle;
  if (uncertainBusy) {
    Serial.println("Flash commands locked until reboot; no automatic retry. Wait for flash to settle before resetting.");
  } else {
    Serial.println("No retry scheduled. A new test requires a fresh confirmation line.");
  }
}

void warning() {
  if(Tcb3Test::Reusable){Serial.println("TCB REUSABLE USB PROTOTYPE: bounded append sessions; battery absent/unarmed; V1 writes and all erases disabled.");return;}
  if(Tcb3Test::Enabled){
    Serial.println(Tcb3Test::Movement?"TCB3 TEST BUILD profile 3B10: exact start 00B000, 120s capture, 180s total deadline, writes 00B000-0FFFFF; V1 writes and all erases disabled.":Tcb3Test::Append?"TCB3 TEST BUILD profile 3B9: exact start 002000, 30s capture, 60s total deadline, writes 002000-03FFFF; V1 writes and all erases disabled.":
      "TCB3 TEST BUILD: V1 writes and all erases disabled; TCB capture limited to 5s and first 64KiB.");return;
  }
  Serial.println("WARNING: destructive flash test is DISARMED.");
  Serial.println("Confirmation permanently erases ALL 4096 bytes at 0xFFF000-0xFFFFFF.");
  Serial.println("It then writes 256 test bytes at 0xFFF000-0xFFF0FF; data is not restored.");
  Serial.println("To authorise exactly ONE test, send this exact line followed by Enter:");
  Serial.println(kConfirmation);
  Serial.println("IMU ON / IMU OFF control printing. Reboot never schedules a destructive test.");
}

bool recheckIdentity() {
  bool matches = true;
  for (uint8_t attempt = 0; attempt < 5; ++attempt) {
    selectFlash();
    SPI.transfer(0x9F);
    const uint8_t manufacturer = SPI.transfer(0);
    const uint8_t type = SPI.transfer(0);
    const uint8_t capacity = SPI.transfer(0);
    deselectFlash();
    Serial.print("Pre-test JEDEC ");
    Serial.print(attempt + 1);
    Serial.print("/5: ");
    hexByte(manufacturer);
    Serial.print(' ');
    hexByte(type);
    Serial.print(' ');
    hexByte(capacity);
    Serial.println();
    matches = matches && manufacturer == 0x0B && type == 0x40 && capacity == 0x18;
  }
  return matches;
}

bool enableWrite() {
  // Check ready before WREN, then verify both WEL=1 and WIP=0 afterward.
  const uint8_t before = readStatus();
  if (before == 0xFF || (before & 1)) {
    fail("flash unresponsive/busy before write-enable", true);
    return false;
  }
  selectFlash();
  SPI.transfer(0x06);
  deselectFlash();
  const uint8_t after = readStatus();
  if (after == 0xFF || (after & 3) != 2) {
    fail("write-enable latch did not set (status/WEL check failed)", true);
    return false;
  }
  return true;
}

// Deliberately fixed-address helpers: no API accepts an arbitrary erase/program
// address or opcode. No chip-erase or status-register-write command exists here.
void eraseTestSector() {
  selectFlash();
  SPI.transfer(0x20);
  sendAddress(kSector);
  deselectFlash();
}

void programTestPage() {
  selectFlash();
  SPI.transfer(0x02);
  sendAddress(kSector);
  for (size_t i = 0; i < kPageSize; ++i) SPI.transfer(pattern(i));
  deselectFlash();
}

bool readTestBytes(uint32_t offset, uint8_t* output, size_t count) {
  if (offset >= kSectorSize || count > kSectorSize - offset) return false;
  selectFlash();
  SPI.transfer(0x03);
  sendAddress(kSector + offset);
  for (size_t i = 0; i < count; ++i) output[i] = SPI.transfer(0);
  deselectFlash();
  return true;
}

void startTest() {
  if(Tcb3Test::Guarded){Serial.println("Diagnostic mutation refused: guarded TCB3 build.");return;}
  // This function is reachable only through the exact serial confirmation.
  Serial.println("Confirmation consumed: ONE flash test requested.");
  if (!recheckIdentity()) {
    fail("JEDEC is not consistently 0B 40 18; no erase/program issued");
    return;
  }
  state = State::Ready;
  operationStartedMs = millis();
  lastPollMs = operationStartedMs - kPollIntervalMs;
}

void serviceInput() {
  const bool connected = bool(Serial);
  if (!connected) {
    inputLength = 0;
    inputOverflow = false;
    wasConnected = false;
    return;
  }
  if (!wasConnected) {
    // Discard input queued before the warning; confirmation must follow it.
    while (Serial.available()) Serial.read();
    if (!Tcb3Session::exclusive()) {
      SerialConsole::printMenu();
      warning();
      if (state != State::Idle) Serial.println("A test is active or flash commands are locked; confirmation is disabled.");
    }
    wasConnected = true;
  }
  // Bound serial work so IMU/LED service continues even with noisy host input.
  for (uint8_t budget = 0; budget < 64 && Serial.available(); ++budget) {
    const char character = static_cast<char>(Serial.read());
    if (state != State::Idle) {
      inputLength = 0;
      inputOverflow = false;
      continue;  // Discard confirmations received while running or faulted.
    }
    if (character == '\r' || character == '\n') {
      input[inputLength] = '\0';
      const bool confirmed = !inputOverflow && strcmp(input, kConfirmation) == 0;
      const bool hadInput = inputLength || inputOverflow;
      inputLength = 0;
      inputOverflow = false;
      if (hadInput && Tcb3Session::guardCommand(input)) continue;
      if (hadInput && !confirmed && SerialConsole::handleCommand(input)) {
        continue;
      }
      if (confirmed) {
        if (Recorder::exclusive()) {
          Serial.println("Diagnostic test refused: recorder is scanning, busy or faulted.");
          continue;
        }
        startTest();
        // A whole RX batch can authorise at most one test, including on failure.
        while (Serial.available()) Serial.read();
        break;
      }
      if (hadInput) Serial.println("Unknown command. Use DIAG, GPS ON/OFF/STATUS/RAW, IMU ON/OFF, RECORD START/STOP/STATUS/READ, or ERASE FFF000.");
    } else if (inputLength < sizeof(input) - 1 && !inputOverflow) {
      input[inputLength++] = character;
    } else {
      inputOverflow = true;  // Reject an entire overlong line, not its suffix.
    }
  }
}

void stepTest() {
  if (state == State::Idle || state == State::Fault) return;
  const unsigned long now = millis();
  if (state == State::Ready || state == State::Erasing || state == State::Programming) {
    if (now - lastPollMs < kPollIntervalMs) return;
    lastPollMs = now;
    const uint8_t status = readStatus();
    if (status == 0xFF) {
      fail("status register returned 0xFF; stopping all flash commands", true);
      return;
    }
    const unsigned long timeout = state == State::Erasing ? kEraseTimeoutMs :
                                  state == State::Programming ? kProgramTimeoutMs : kReadyTimeoutMs;
    if (status & 1) {
      if (now - operationStartedMs >= timeout) fail("WIP/busy polling timed out", true);
      return;
    }
    if (state == State::Ready) {
      if (!enableWrite()) return;
      Serial.println("Erasing ONLY 0xFFF000-0xFFFFFF (0x20).");
      eraseTestSector();
      operationStartedMs = millis();
      state = State::Erasing;
    } else if (state == State::Erasing) {
      if (status & 2) {
        fail("erase completed without clearing WEL; erase may have been rejected", true);
        return;
      }
      eraseOffset = 0;
      state = State::VerifyErase;
      Serial.println("Erase ready; checking all 4096 bytes for 0xFF.");
    } else {
      if (status & 2) {
        fail("program completed without clearing WEL; command may have been rejected", true);
        return;
      }
      if (!readTestBytes(0, readback, sizeof(readback))) {
        fail("readback range check failed");
        return;
      }
      compareOffset = 0;
      mismatchCount = 0;
      state = State::Compare;
    }
  } else if (state == State::VerifyErase) {
    if (!readTestBytes(eraseOffset, readback, sizeof(readback))) {
      fail("erase verification range check failed");
      return;
    }
    for (size_t i = 0; i < sizeof(readback); ++i) {
      if (readback[i] != 0xFF) {
        Serial.print("Erase verification mismatch at ");
        hexAddress(kSector + eraseOffset + i);
        Serial.print(" expected=0xFF actual=");
        hexByte(readback[i]);
        Serial.println();
        fail("sector did not erase; no page program issued");
        return;
      }
    }
    eraseOffset += sizeof(readback);
    if (eraseOffset == kSectorSize) {
      if (!enableWrite()) return;
      Serial.println("Programming 256 bytes at 0xFFF000 (pattern: offset XOR 0xA5).");
      programTestPage();
      operationStartedMs = millis();
      lastPollMs = operationStartedMs;
      state = State::Programming;
    }
  } else if (state == State::Compare) {
    // Report every mismatch, in small batches to keep IMU/LED service moving.
    for (uint8_t budget = 0; budget < 8 && compareOffset < kPageSize; ++budget, ++compareOffset) {
      const uint8_t expected = pattern(compareOffset);
      if (readback[compareOffset] != expected) {
        ++mismatchCount;
        Serial.print("Mismatch at ");
        hexAddress(kSector + compareOffset);
        Serial.print(" expected=");
        hexByte(expected);
        Serial.print(" actual=");
        hexByte(readback[compareOffset]);
        Serial.println();
      }
    }
    if (compareOffset == kPageSize) {
      Serial.print(mismatchCount == 0 ? "FLASH TEST PASS" : "FLASH TEST FAIL");
      Serial.print(": compared all 256 bytes; mismatches=");
      Serial.println(mismatchCount);
      Serial.println("Test finished. No automatic repeat. Send a fresh ERASE FFF000 line to run once again.");
      state = State::Idle;
    }
  }
}
}

namespace FlashWriteTest {
void update() {
  serviceInput();
  if (!Tcb3Session::exclusive()) stepTest();
}

bool diagnosticsAllowed() {
  return state == State::Idle;
}

const char* diagnosticState() {
  switch (state) {
    case State::Idle: return "Idle";
    case State::Ready: return "Ready";
    case State::Erasing: return "Erasing";
    case State::VerifyErase: return "VerifyErase";
    case State::Programming: return "Programming";
    case State::Compare: return "Compare";
    case State::Fault: return "Fault";
  }
  return "Unknown";
}
}
