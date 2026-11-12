#pragma once
#include "Arduino.h"
#define MSBFIRST 1
#define SPI_MODE0 0
struct SPISettings {
  uint32_t hz;
  SPISettings(uint32_t frequency, int, int) : hz(frequency) {}
};
struct MockSPI {
  // Sparse mock: first 16 sectors and final recording sector have backing RAM.
#ifdef TCB_SIM_FLASH_BYTES
  uint8_t memory[TCB_SIM_FLASH_BYTES];
#else
  uint8_t memory[65536];
#endif
  uint8_t finalSector[4096];
  uint8_t tx[260];
  unsigned position = 0, programs = 0, erases = 0, violations = 0, enables = 0;
  unsigned programDelayMs = 3, eraseDelayMs = 20;
  bool transactionActive=false;uint32_t hz=1000000;
  bool deepPower=false;unsigned sleeps=0,releases=0,asleepAccesses=0;
  bool failRelease=false,wakePending=false;
  uint32_t wakeReadyUs=0;
  unsigned wakeDelayViolations=0,jedecReads=0;
#if TACTICPOD_TCB3_TEST_BUILD || TACTICPOD_REUSABLE
  uint8_t testSr2=2;
  uint32_t highestProgramEnd=0;
#endif
  bool wel = false, denyWel = false, stuckBusy = false, wrongId = false;
  bool rejectErase = false, corruptProgram = false;
  uint32_t busyUntil = 0;
  uint8_t& byte(uint32_t at) {
    if (at < sizeof(memory)) return memory[at];
    if (at >= 0xFFE000 && at < 0xFFF000) return finalSector[at - 0xFFE000];
    static uint8_t empty = 0xFF;
    if (at >= 0xFFF000) ++violations;
    empty = 0xFF; return empty;
  }
  void begin() {}
  void beginTransaction(SPISettings settings) { if(transactionActive)++violations;transactionActive=true;position = 0; hz = settings.hz; if (hz != 1000000 && hz != 4000000) ++violations; }
  uint32_t address() const { return uint32_t(tx[1]) << 16 | uint32_t(tx[2]) << 8 | tx[3]; }
  uint8_t transfer(uint8_t value) {
#ifdef TCB_SIM_SPI_TRANSFER
    TCB_SIM_SPI_TRANSFER();
#endif
    const unsigned i = position++;
    if (i < sizeof(tx)) tx[i] = value; else ++violations;
    if (i == 0) {
      if (hz != 1000000 && value != 0x03) ++violations; // 4 MHz is only for export reads.
#if TACTICPOD_REUSABLE
      if(wakePending && value!=0xAB){
        if(int32_t(mockTime*1000+mockSubUs-wakeReadyUs)<0)++wakeDelayViolations;
        else wakePending=false;
      }
      if (deepPower && value != 0xAB) ++asleepAccesses, ++violations;
      if (value == 0xB9 || value == 0xAB) return 0;
#endif
      if (value != 0x03 && value != 0x05 && value != 0x06 && value != 0x02 && value != 0x20 && value != 0x9F && value != 0x5A
#if TACTICPOD_TCB3_TEST_BUILD || TACTICPOD_REUSABLE
          && value!=0x35
#endif
          ) ++violations;
      return 0;
    }
#if TACTICPOD_REUSABLE
    if (deepPower) return 0xFF;
#endif
    if (tx[0] == 0x9F) { if(i==1)++jedecReads;return i == 1 ? (wrongId ? 0 : 0x0B) : i == 2 ? 0x40 : 0x18; }
    if (tx[0] == 0x05) return ((stuckBusy || int32_t(mockTime - busyUntil) < 0) ? 1 : 0) | (wel ? 2 : 0);
#if TACTICPOD_TCB3_TEST_BUILD || TACTICPOD_REUSABLE
    if(tx[0]==0x35)return testSr2;
#endif
    if (tx[0] == 0x03 && i >= 4) return byte(address() + i - 4);
    return 0;
  }
  void endTransaction() {
    if(!transactionActive)++violations;transactionActive=false;
#if TACTICPOD_REUSABLE
    if (position == 1 && tx[0] == 0xB9) {
      if (wel || stuckBusy || int32_t(mockTime - busyUntil) < 0) ++violations;
      deepPower = true; ++sleeps; return;
    }
    if (position == 1 && tx[0] == 0xAB) {
      if(!failRelease){deepPower=false;wakePending=true;wakeReadyUs=mockTime*1000+mockSubUs+100;}
      ++releases;return;
    }
#endif
    if (tx[0] == 0x06) { ++enables; wel = !denyWel; }
    if (tx[0] == 0x02 || tx[0] == 0x20) {
      if (!wel || address() >= 0xFFF000) { ++violations; return; }
      wel = false;
      if (tx[0] == 0x02) {
#if TACTICPOD_TCB3_TEST_BUILD || TACTICPOD_REUSABLE
        uint32_t end=address()+position-4;if(end>highestProgramEnd)highestProgramEnd=end;
#endif
        ++programs;
        if ((address() & 255) + position - 4 > 256) ++violations;
        for (unsigned i = 4; i < position; ++i) byte(address() + i - 4) &= tx[i];
        if (corruptProgram) byte(address()) ^= 1;
        busyUntil = mockTime + programDelayMs;
      } else {
        ++erases;
        if (address() & 4095) ++violations;
        if (!rejectErase) for (unsigned i = 0; i < 4096; ++i) byte(address() + i) = 0xFF;
        busyUntil = mockTime + eraseDelayMs;
      }
    }
  }
};
extern MockSPI SPI;
