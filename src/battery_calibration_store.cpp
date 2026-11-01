#include "battery_calibration_store.h"

#include <stddef.h>
#include <string.h>

#if !defined(TACTICPOD_BATTERY_SIM) && !defined(TACTICPOD_BATTERY_STORE_SIM)
#include <nrf.h>
#endif

namespace BatteryCalibrationStore {
namespace {
constexpr uint32_t kMagic = 0x31435442; // "BTC1"
constexpr uintptr_t kPageA = 0x000EB000;
constexpr uintptr_t kPageB = 0x000EC000;
constexpr unsigned kWords = 5;

struct Record {
  uint32_t magic;
  uint32_t sequence;
  uint32_t gainPpm;
  int32_t offsetMv;
  uint32_t checksum;
};

uint32_t crc32(const uint32_t* words, unsigned count) {
  uint32_t crc = 0xFFFFFFFF;
  for (unsigned i = 0; i < count; ++i) {
    uint32_t value = words[i];
    for (unsigned byte = 0; byte < 4; ++byte) {
      crc ^= value & 0xFF;
      value >>= 8;
      for (unsigned bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

bool valid(const Record& record) {
  const uint32_t* words = reinterpret_cast<const uint32_t*>(&record);
  return record.magic == kMagic &&
    record.gainPpm >= 800000 && record.gainPpm <= 1200000 &&
    record.offsetMv >= -200 && record.offsetMv <= 200 &&
    record.checksum == crc32(words, kWords - 1);
}

uintptr_t address(unsigned page) { return page ? kPageB : kPageA; }

#if defined(TACTICPOD_BATTERY_SIM) || defined(TACTICPOD_BATTERY_STORE_SIM)
uint32_t simulatedPages[2][kWords] = {
  {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF},
  {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}
};

void readRecord(unsigned page, Record& record) {
  memcpy(&record, simulatedPages[page], sizeof(record));
}

bool pageEmpty(unsigned page) {
  for (unsigned i = 0; i < kWords; ++i)
    if (simulatedPages[page][i] != 0xFFFFFFFF) return false;
  return true;
}

bool writeRecord(unsigned page, const Record& record) {
  for (unsigned i = 0; i < kWords; ++i) {
    const uint32_t value = reinterpret_cast<const uint32_t*>(&record)[i];
    if ((simulatedPages[page][i] & value) != value) return false;
    simulatedPages[page][i] &= value;
  }
  return true;
}
#else
void waitReady() {
  while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
}

void readRecord(unsigned page, Record& record) {
  const volatile uint32_t* source =
    reinterpret_cast<const volatile uint32_t*>(address(page));
  uint32_t* destination = reinterpret_cast<uint32_t*>(&record);
  for (unsigned i = 0; i < kWords; ++i) destination[i] = source[i];
}

bool pageEmpty(unsigned page) {
  const volatile uint32_t* words =
    reinterpret_cast<const volatile uint32_t*>(address(page));
  for (unsigned i = 0; i < kWords; ++i)
    if (words[i] != 0xFFFFFFFF) return false;
  return true;
}

bool writeRecord(unsigned page, const Record& record) {
  const uintptr_t target = address(page);
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
  waitReady();
  NRF_NVMC->ERASEPAGE = target;
  waitReady();
  volatile uint32_t* destination = reinterpret_cast<volatile uint32_t*>(target);
  const uint32_t* source = reinterpret_cast<const uint32_t*>(&record);
  for (unsigned i = 0; i < kWords; ++i) {
    destination[i] = source[i];
    waitReady();
  }
  NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren;
  waitReady();
  Record check{};
  readRecord(page, check);
  return memcmp(&check, &record, sizeof(record)) == 0;
}
#endif
}

LoadResult load(Settings& settings) {
  Record records[2]{};
  bool empty[2]{};
  for (unsigned i = 0; i < 2; ++i) {
    readRecord(i, records[i]);
    empty[i] = pageEmpty(i);
  }
  const bool goodA = valid(records[0]);
  const bool goodB = valid(records[1]);
  if (!goodA && !goodB)
    return empty[0] && empty[1] ? LoadResult::Empty : LoadResult::Invalid;
  const unsigned selected = goodA && goodB
    ? (static_cast<int32_t>(records[1].sequence - records[0].sequence) > 0 ? 1 : 0)
    : (goodB ? 1 : 0);
  settings.sequence = records[selected].sequence;
  settings.gainPpm = records[selected].gainPpm;
  settings.offsetMv = records[selected].offsetMv;
  return LoadResult::Valid;
}

bool save(const Settings& settings) {
  if (settings.gainPpm < 800000 || settings.gainPpm > 1200000 ||
      settings.offsetMv < -200 || settings.offsetMv > 200) return false;
  Settings current{};
  const LoadResult loaded = load(current);
  if (loaded == LoadResult::Invalid) return false;

  Record records[2]{};
  readRecord(0, records[0]);
  readRecord(1, records[1]);
  const bool goodA = valid(records[0]);
  const bool goodB = valid(records[1]);
  const unsigned target = !goodA ? 0 : (!goodB ? 1 :
    (static_cast<int32_t>(records[1].sequence - records[0].sequence) > 0 ? 0 : 1));

  Record next{};
  next.magic = kMagic;
  next.sequence = loaded == LoadResult::Valid ? current.sequence + 1 : 1;
  next.gainPpm = settings.gainPpm;
  next.offsetMv = settings.offsetMv;
  next.checksum = crc32(reinterpret_cast<const uint32_t*>(&next), kWords - 1);
  if (!writeRecord(target, next)) return false;
  Record verify{};
  readRecord(target, verify);
  return valid(verify) && verify.sequence == next.sequence &&
    verify.gainPpm == next.gainPpm && verify.offsetMv == next.offsetMv;
}

#if defined(TACTICPOD_BATTERY_SIM) || defined(TACTICPOD_BATTERY_STORE_SIM)
void resetForTest() {
  for (auto& page : simulatedPages)
    for (uint32_t& word : page) word = 0xFFFFFFFF;
}

void corruptForTest(unsigned page, unsigned word) {
  if (page < 2 && word < kWords) simulatedPages[page][word] ^= 1U;
}
#endif
}
