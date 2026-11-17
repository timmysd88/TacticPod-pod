#pragma once

namespace GpsReceiver {
void begin();
void update();
bool handleCommand(const char* line);
bool hasFreshFix();
// Standby support: MAX-M10S software standby, VCC/V_IO remain supplied.
bool busy();      // Start-up profile, scan, config or rate measurement still running.
bool suspend();   // Guarded PMREQ then UART stop; false on ownership/TX failure.
void resume();    // Bounded UART wake/baud recovery, then fresh RAM profile verification.
// True while SCAN, CONFIG or profile changes require flash/recording idle.
bool uartScanBusy();
bool uartScanActive();
bool configurationActive();
bool profileAuditActive();
// Deferred field start: fresh RAM audit/recovery must finish before capture.
bool prepareRecording();
bool recordingReady();
bool preparationFailed();
}
