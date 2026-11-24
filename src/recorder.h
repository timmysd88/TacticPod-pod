#pragma once
#include <stdint.h>

namespace Recorder {
void begin(bool imuReady);
void update();
bool handleCommand(const char* line);
bool recording();
bool exclusive();
bool faulted();
const char* faultReason();
const char* startupFaultReason();
const char* diagnosticStage();
bool exporting();
void addSample(uint32_t timestampMs, const int16_t axes[6]);
void noteMissed(uint32_t count);
}
