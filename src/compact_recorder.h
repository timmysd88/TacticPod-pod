#pragma once
#include <stddef.h>
#include <stdint.h>

// Phase 3A only: no SPI, no flash writer, no RECORD command ownership.
namespace CompactRecorder {
constexpr unsigned kInputBytes = 8192, kRecords = 256, kFrameBytes = 8228;
constexpr unsigned kUnitsPerSlice = 8, kSliceUs = 200;
struct Stats {
  uint64_t accepted, rejected, sealed, completed, consumed, rawFallbacks;
  uint64_t backpressureSlices, workUs, maxBlockWorkUs, maxSliceUs;
  uint32_t maxUnits, inputHigh, outputHigh;
  uint8_t inputUsed, outputReady;
  bool capturing, fault;
};
bool start(); // Refuses to discard any pending source/output; CLEAR explicitly.
void stop();  // Seals the retained partial block; compression continues in slices.
bool clear(); // Only when not capturing. RAM only; resets this pipeline, not clock.
bool capturing();
void observe(const uint8_t* record, size_t length); // <=44-byte immutable copy.
void update(bool allowWork = true);
// Immutable output lease, valid until release/explicit CLEAR. No automatic sink.
const uint8_t* output(size_t& length);
void release();
Stats stats();
bool handleCommand(const char* command);
}
