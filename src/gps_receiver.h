#pragma once

namespace GpsReceiver {
void begin();
void update();
bool handleCommand(const char* line);
bool hasFreshFix();
// Standby support. The module itself stays powered; only the MCU UART is stopped.
bool busy();      // Start-up profile, scan, config or rate measurement still running.
void suspend();   // Stop UARTE RX/TX and parsing; never changes receiver configuration.
void resume();    // Restart UART at the normal baud; re-arms start-up only if it never reached the GPS.
// True while SCAN, CONFIG or profile changes require flash/recording idle.
bool uartScanBusy();
// Deferred field start: fresh RAM audit/recovery must finish before capture.
bool prepareRecording();
bool recordingReady();
bool preparationFailed();
}
