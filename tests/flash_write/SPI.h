#pragma once
#include "Arduino.h"
#define MSBFIRST 1
#define SPI_MODE0 0
struct SPISettings {
  uint32_t hz;
  SPISettings(uint32_t frequency, int, int) : hz(frequency) {}
};
struct MockSPI {
#ifdef FLASH_ID_TEST
  uint8_t sfdp[256]{},trace[80][24]{};
  unsigned traceSize[80]{},traceCount=0,sfdpReads=0;
  bool unstableSfdp=false,unavailableStatus=false;
#endif
  uint8_t memory[4096] = {};
  uint8_t tx[260] = {};
  size_t position = 0;uint32_t hz = 1000000;
  unsigned eraseCount = 0, programCount = 0, enableCount = 0;
  unsigned violations = 0, jedecCount = 0;
  bool wel = false, wrongId = false, denyWel = false, stuckBusy = false;
  bool corruptReadback = false, failedErase = false;
  bool stallErase = false, stallProgram = false;
  uint32_t busyUntil = 0;
  void begin() {}
  void beginTransaction(SPISettings settings) { hz = settings.hz; if (hz != 1000000 && hz != 4000000) ++violations; position = 0; }
  uint32_t address() { return (uint32_t(tx[1]) << 16) | (uint32_t(tx[2]) << 8) | tx[3]; }
  uint8_t transfer(uint8_t value) {
    size_t index = position++;
    if (index < sizeof(tx)) tx[index] = value; else ++violations;
    if (index == 0) {
      if (hz != 1000000 && value != 0x03) ++violations; // 4 MHz is only for export reads.
      if (value != 0x9F && value != 0x5A && value != 0x05 && value != 0x06 && value != 0x20 && value != 0x02 && value != 0x03
#ifdef FLASH_ID_TEST
          && value!=0x35 && value!=0x90
#endif
          ) ++violations;
      if (value == 0x9F) ++jedecCount;
      return 0;
    }
#ifdef FLASH_ID_TEST
    if(tx[0]==5 && unavailableStatus)return 255;
    if(tx[0]==0x35)return 0x42;
    if(tx[0]==0x90 && index>=4)return index==4?0x0b:0x17;
    if(tx[0]==0x5a && index>=5){unsigned at=address()+index-5;if(at>=256){++violations;return 255;}return sfdp[at]^(unstableSfdp && sfdpReads>=16?1:0);}
#endif
    if (tx[0] == 0x9F) return index == 1 ? (wrongId ? 0xEF : 0x0B) : index == 2 ? 0x40 : 0x18;
    if (tx[0] == 0x05) return (stuckBusy || mockTime < busyUntil ? 1 : 0) | (wel ? 2 : 0);
    if (tx[0] == 0x03 && index >= 4) {
      const uint32_t offset = address() - 0xFFF000 + index - 4;
      if (offset >= 4096) { ++violations; return 0; }
      const uint8_t data = memory[offset];
      // Corrupt two bytes only after programming; must report BOTH addresses.
      return corruptReadback && programCount && (offset == 0 || offset == 255) ? data ^ 1 : data;
    }
    return 0;
  }
  // Mirrors Adafruit nRF52 SPIClass::transfer(tx,rx,count): null tx clocks zeros, null rx discards.
  // Each byte still goes through the byte-level protocol checks above.
  unsigned bulkTransfers = 0;
  void transfer(const void* txBuffer, void* rxBuffer, size_t count) {
    ++bulkTransfers;
    const uint8_t* t = static_cast<const uint8_t*>(txBuffer);uint8_t* r = static_cast<uint8_t*>(rxBuffer);
    for (size_t i = 0; i < count; ++i) { const uint8_t v = transfer(t ? t[i] : uint8_t(0)); if (r) r[i] = v; }
  }
  void endTransaction() {
#ifdef FLASH_ID_TEST
    if(traceCount<80){traceSize[traceCount]=position;memcpy(trace[traceCount++],tx,position<24?position:24);}else ++violations;
    if(tx[0]==0x5a)++sfdpReads;
#endif
    if (tx[0] == 0x06) { ++enableCount; wel = !denyWel; }
    if (tx[0] == 0x20 || tx[0] == 0x02) {
      if (!wel || address() != 0xFFF000) { ++violations; return; }
      wel = false;
      if (tx[0] == 0x20) {
        ++eraseCount;
        if (stallErase) stuckBusy = true;
        if (!failedErase) memset(memory, 0xFF, sizeof(memory));
        busyUntil = mockTime + 200;
      } else {
        ++programCount;
        if (stallProgram) stuckBusy = true;
        if (position != 260) ++violations;
        for (size_t i = 0; i < 256; ++i) memory[i] &= tx[4 + i];
        busyUntil = mockTime + 10;
      }
    }
  }
};
extern MockSPI SPI;
