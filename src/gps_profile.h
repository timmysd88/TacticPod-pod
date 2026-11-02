#pragma once
#include <stddef.h>
#include <stdint.h>
namespace GpsProfile {
bool startTest();
bool startStartup();
const char* status();
const char* error();
// Fresh RAM readback; recovery uses the same ACK/readback/rollback engine.
bool startCheck(bool recover);
void invalidate(const char* reason);
bool currentVerified();
uint32_t checkedAt();
const char* lastApplied();
bool automaticStartup();
void observeCurrent(uint32_t key, uint32_t value);
bool startRestore();
bool active();
void update();
// Called only for bounded, checksum-validated frames by the single UBX reader.
void onFrame(uint8_t cls, uint8_t id, const uint8_t* payload, size_t length);
}
