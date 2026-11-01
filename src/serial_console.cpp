#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <string.h>
#include "serial_console.h"
#include "recorder.h"
#include "flash_diagnostics.h"
#include "gps_receiver.h"
#include "ram_observations.h"
#include "compact_recorder.h"
#include "tcb3_session.h"
#include "tcb3_test_limits.h"

namespace {
bool printImu = false;
}

namespace SerialConsole {
void printMenu() {
  Serial.println("TacticPod command menu (115200 baud; finish each command with Enter):");
  Serial.println("  IMU ON       - print IMU readings at 10 Hz");
  Serial.println("  IMU OFF      - stop printing; sampling continues (100 Hz when recording)");
  if(Tcb3Test::Reusable)Serial.println("  RECORD STATUS / READ - legacy read-only compatibility; use TCB for new sessions");
  else Serial.println("  RECORD START / RECORD STOP / RECORD STATUS - append binary IMU at 100 Hz");
  Serial.println("  RECORD READ  - export committed recordings as CSV while stopped");
  if(!Tcb3Test::Reusable)Serial.println("  RECORD ERASE - warn before explicitly confirmed recording-area erase");
  Serial.println("  DIAG         - display JEDEC and SFDP information once (while idle)");
  Serial.println("  DIAG ID      - read-only raw flash identity/SFDP evidence (OBS/COMP stopped)");
  Serial.println("  GPS ON / GPS OFF / GPS STATUS - GPS output (OFF at boot); UART always receives");
  Serial.println("  GPS RAW      - capture 10 seconds of UART bytes without configuring GPS");
  Serial.println("  GPS SCAN     - stopped-only, receive-only six-baud scan; restores 38400");
  Serial.println("  GPS RATE     - measure separate GGA/RMC cadence for 15 seconds, no fix needed");
  Serial.println("  GPS CONFIG   - stopped-only, read current receiver settings using UBX polls");
  Serial.println("  GPS 10HZ TEST - stopped-only, snapshot then verified RAM-only GPS+Galileo test");
  Serial.println("  GPS RESTORE  - stopped-only, restore and verify the saved GPS RAM snapshot");
  Serial.println("  OBS START / STOP / CLEAR / STATUS - bounded RAM observations; no flash writes");
  Serial.println("  COMP START / STOP / CLEAR / STATUS - opt-in RAM compression; no flash writes");
  if(Tcb3Test::Reusable){
    Serial.println("  TCB START / STOP / STATUS / READ - bounded append-only compact sessions");
    Serial.println("  TCB DURATION seconds - stopped only, 1..1800; default 120; drain deadline 60 seconds");
    Serial.println("  BAT STATUS - voltage, calibration, charger indicator, USB VBUS and protection");
    Serial.println("  BAT CAL measured_mV - stopped-only RAM calibration");
    Serial.println("  BAT CAL SAVE measured_mV - persist confirmed multimeter calibration");
    Serial.println("  BAT MODE USB|CELL - stopped-only RAM protection selection");
    Serial.println("  BAT LIMITS warn stop hard recover - stopped-only millivolt thresholds");
  }else Serial.println("  TCB START / STOP / STATUS / READ - separate compact sessions (host simulation gate)");
  Serial.println("  UART LOOPBACK TEST - explicit D6-to-D7 test; disconnect GPS UART first");
  if(!Tcb3Test::Reusable)Serial.println("  ERASE FFF000 - confirm ONE destructive flash test (see warning below)");
  Serial.print("IMU printing: ");
  Serial.println(printImu ? "ON" : "OFF (default after boot)");
}

bool handleCommand(const char* line) {
  if (Tcb3Session::guardCommand(line)) return true;
  if (Tcb3Session::handleCommand(line)) return true;
  if (CompactRecorder::handleCommand(line)) return true;
  if (RamObservations::handleCommand(line)) return true;
  if (GpsReceiver::handleCommand(line)) return true;
  if (GpsReceiver::uartScanBusy() &&
      (strcmp(line, "ERASE FFF000") == 0 || strcmp(line, "DIAG") == 0 ||
       (strncmp(line, "RECORD ", 7) == 0 && strcmp(line, "RECORD STATUS") != 0 && strcmp(line, "RECORD STOP") != 0))) {
    Serial.println("Command refused while GPS UART scan/config/profile owns diagnostics; recording and flash must remain stopped.");
    return true;
  }
  if (strcmp(line, "DIAG") == 0) {
    FlashDiagnostics::request();
    return true;
  }
  if (strcmp(line, "DIAG ID") == 0) {
    FlashDiagnostics::requestIdentity();
    return true;
  }
  if (Recorder::handleCommand(line)) return true;
  if (strcmp(line, "IMU ON") == 0) {
    printImu = true;
    Serial.println("IMU printing ON at 10 Hz; recording acquisition stays at 100 Hz.");
    return true;
  }
  if (strcmp(line, "IMU OFF") == 0) {
    printImu = false;
    Serial.println("IMU printing OFF; sampling continues (100 Hz while recording).");
    return true;
  }
  return false;
}

bool imuPrintingEnabled() {
  return printImu;
}
}
