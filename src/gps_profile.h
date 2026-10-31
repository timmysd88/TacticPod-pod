#pragma once
#include <stddef.h>
#include <stdint.h>
namespace GpsProfile {
bool startTest();
bool startStartup();
const char* status();
const char* error();
bool startRestore();
bool active();
void update();
// Called only for bounded, checksum-validated frames by the single UBX reader.
void onFrame(uint8_t cls, uint8_t id, const uint8_t* payload, size_t length);
}
