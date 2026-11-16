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
#include "field_trace.h"
#include "device_identity.h"
#include "flash_write_test.h"

namespace {
// Share the existing flag byte; keep the normal-build static RAM budget.
struct { bool printImu : 1; bool deviceInfoPending : 1; } consoleFlags{};
uint32_t deviceInfoRequestedMs = 0;
constexpr uint32_t kDeviceInfoTimeoutMs = 2000;
bool deviceInfoAllowed() {
  const auto compact = CompactRecorder::stats();
  return Serial && FlashWriteTest::diagnosticsAllowed() && !Recorder::exclusive() &&
    !Recorder::exporting() && !Tcb3Session::exclusive() && !Tcb3Session::busy() &&
    !RamObservations::capturing() && !compact.capturing && !compact.inputUsed && !compact.outputReady &&
    !GpsReceiver::busy() && !GpsReceiver::uartScanBusy();
}
}

namespace SerialConsole {
void serviceDeviceInfo() {
  if (!consoleFlags.deviceInfoPending) return;
  if (!deviceInfoAllowed() || uint32_t(millis() - deviceInfoRequestedMs) >= kDeviceInfoTimeoutMs) {
    consoleFlags.deviceInfoPending = false; return;
  }
  char response[DeviceIdentity::InfoCapacity];
  DeviceIdentity::Id identity;
  size_t length;
  if (DeviceIdentity::read(identity)) {
#if TACTICPOD_BLE
    constexpr bool bleBuild = true;
#else
    constexpr bool bleBuild = false;
#endif
    length = DeviceIdentity::info(identity, bleBuild, response, sizeof(response));
  } else {
    const char unavailable[] = "\n{\"type\":\"device_info\",\"version\":1,\"status\":\"unavailable\"}\n";
    memcpy(response, unavailable, sizeof(unavailable)); length = sizeof(unavailable) - 1;
  }
  if (!length) { consoleFlags.deviceInfoPending = false; return; }
  // Current nRF TinyUSB TX FIFO is 256 bytes. A whole line must fit before write,
  // avoiding its blocking write/yield loop and keeping other output out of JSON.
  if (Serial.availableForWrite() < int(length)) return;
  consoleFlags.deviceInfoPending = false;
  Serial.write(reinterpret_cast<const uint8_t*>(response), length);
}
void printMenu() {
  Serial.println("TacticPod command menu (115200 baud; finish each command with Enter):");
  Serial.println("  DEVICE INFO  - read factory identity JSON v1 while idle");
  Serial.println("  IMU ON       - print IMU readings at 10 Hz");
  Serial.println("  IMU OFF      - stop printing; sampling continues (100 Hz when recording)");
  if(Tcb3Test::Reusable)Serial.println("  RECORD STATUS / READ - legacy read-only compatibility; use TCB for new sessions");
  else Serial.println("  RECORD START / RECORD STOP / RECORD STATUS - append binary IMU at 100 Hz");
  Serial.println("  RECORD READ  - export committed recordings as CSV while stopped");
  if(!Tcb3Test::Reusable)Serial.println("  RECORD ERASE - warn before explicitly confirmed recording-area erase");
  else Serial.println("  RECORD RESET - fast logical reset (then RECORD RESET CONFIRM); hides all recordings");
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
    Serial.println("  TCB DURATION seconds|UNLIMITED - stopped only, 1..1800 or until stop; default 120; drain deadline 60 seconds");
    Serial.println("  BUTTON D0: hold 3s to start; tap, release, then hold 3s within 5s to stop");
    Serial.println("  FIELD EVENTS - drain bounded timestamped RAM events while stopped; overwritten count explicit");
    Serial.println("  Button starts record until stopped. Idle off-USB: System ON standby (GPS UART/IMU/flash/LED off); USB keeps it awake");
    Serial.println("  BAT STATUS - raw/filtered voltage, estimated charge + category (UNKNOWN unless trusted), trust, protection; no battery presence inference");
    Serial.println("  BAT CAL measured_mV / BAT MODE USB|CELL - stopped-only RAM calibration/arming");
    Serial.println("  BAT LIMITS warn stop hard recover - stopped-only millivolt thresholds");
  }else Serial.println("  TCB START / STOP / STATUS / READ - separate compact sessions (host simulation gate)");
  Serial.println("  UART LOOPBACK TEST - explicit D6-to-D7 test; disconnect GPS UART first");
  if(!Tcb3Test::Reusable)Serial.println("  ERASE FFF000 - confirm ONE destructive flash test (see warning below)");
  Serial.print("IMU printing: ");
  Serial.println(consoleFlags.printImu ? "ON" : "OFF (default after boot)");
}

bool handleCommand(const char* line) {
  // A new command owns subsequent output. Never defer identity into its export.
  if (strcmp(line, "DEVICE INFO") != 0) consoleFlags.deviceInfoPending = false;
  if (Tcb3Session::guardCommand(line)) { consoleFlags.deviceInfoPending = false; return true; }
  if (strcmp(line, "DEVICE INFO") == 0) {
    if (!deviceInfoAllowed()) { consoleFlags.deviceInfoPending = false; return true; }
    if (!consoleFlags.deviceInfoPending) deviceInfoRequestedMs = millis();
    consoleFlags.deviceInfoPending = true;
    return true;
  }
#if TACTICPOD_REUSABLE
  if(!strcmp(line,"FIELD EVENTS")) {FieldTrace::buffer().remaining=FieldTrace::buffer().count;return true;}
#endif
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
    consoleFlags.printImu = true;
    Serial.println("IMU printing ON at 10 Hz; recording acquisition stays at 100 Hz.");
    return true;
  }
  if (strcmp(line, "IMU OFF") == 0) {
    consoleFlags.printImu = false;
    Serial.println("IMU printing OFF; sampling continues (100 Hz while recording).");
    return true;
  }
  return false;
}

bool imuPrintingEnabled() {
  return consoleFlags.printImu;
}
}
