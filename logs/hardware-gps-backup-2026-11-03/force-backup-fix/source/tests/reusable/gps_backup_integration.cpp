// Actual receiver, parser, profile transactions and recorder guards; only
// hardware/time and the receiver's replies are modelled. Never opens a device.
#define TACTICPOD_GPS_BACKUP_TEST 1
#define TACTICPOD_REUSABLE 1
#define TACTICPOD_BATTERY_SIM 1
#define TCB_SIM_FLASH_BYTES 0x1000000
#include "../tcb3/integration.cpp"
#include "../../src/battery_runtime.cpp"

ProfileModel::Receiver backupReceiver(0);
bool asleep = false, silence = false, wakeEnabled = true, validEnabled = true;
bool profileReplies = true;
uint32_t nextNmea = 0;
unsigned backupRequests = 0, backupWakes = 0;

void backupTick() {
  if (!suspended) {
    if (backupReceiver.handled < Serial1.writeCalls) {
      const unsigned at = backupReceiver.handled;
      const uint8_t* p = Serial1.writes[at];
      const unsigned n = Serial1.writeSizes[at];
      if (n == 24 && p[2] == 2 && p[3] == 0x41) {
        const uint8_t expected[24] = {0xB5,0x62,0x02,0x41,0x10,0,0,0,0,0,0,0,0,0,
                                     0x06,0,0,0,0x08,0,0,0,0x61,0x6B};
        if (memcmp(p, expected, sizeof(expected))) ++backupReceiver.invalid;
        ++backupReceiver.handled; ++backupRequests; asleep = true;
      } else if (n == 4 && p[0] == 0xff && p[1] == 0xff && p[2] == 0xff && p[3] == 0xff) {
        ++backupReceiver.handled; ++backupWakes;
        if (wakeEnabled) asleep = false;
      } else if (!asleep && profileReplies) backupReceiver.service();
      else ++backupReceiver.handled; // Deliberately withhold profile replies.
    }
    if (!asleep && !silence && uint32_t(millis() - nextNmea) < 0x80000000u) {
      // Independent checksum-valid/invalid NMEA goes through production parsing.
      rateFrame(false, millis(), false, false, validEnabled);
      nextNmea = millis() + 100;
    }
  }
  loop();
  advanceUs(Recorder::exclusive() || Tcb3Session::exclusive() ? 100 : 10000);
}

bool finishBackup(unsigned timeout = 90000) {
  const uint32_t at = millis();
  while (backupTest.running() && uint32_t(millis() - at) < timeout) {
    // Both recording admission paths must stay blocked during recovery.
    if (GpsReceiver::recordingReady() || GpsReceiver::prepareRecording()) return false;
    backupTick();
  }
  return backupTest.done() && !SPI.programs && !SPI.erases && !SPI.enables && !SPI.violations;
}

extern "C" unsigned backupIntegrationCase(unsigned c) {
  memset(SPI.memory, 255, sizeof(SPI.memory));
  memset(SPI.finalSector, 255, sizeof(SPI.finalSector));
  memcpy(backupReceiver.db, ProfileModel::desired, sizeof(backupReceiver.db));
  Serial.connected = true; setup();
  const uint32_t at = millis();
  while ((!GpsReceiver::recordingReady() || Tcb3Session::exclusive() || Recorder::exclusive()) &&
         uint32_t(millis() - at) < 30000) backupTick();
  if (!GpsReceiver::recordingReady() || !GpsProfile::currentVerified()) return 1;
  clear();
  wakeEnabled = c != 1 && c != 6;
  validEnabled = c != 2;
  profileReplies = c != 3;
  SerialConsole::handleCommand("GPS BACKUP TEST");
  if (!backupTest.running()) return 2;
  if (c >= 7 && c <= 9) {
    while (backupTest.running() && backupTest.current() != GpsPower::BackupTest::Phase::Listen) backupTick();
    if (!backupTest.running()) return 50;
    const size_t before = Serial.used;
    if (c == 7 || c == 9) nmea("GPRMC,000000,V,,,,,,,010100,,,", c == 7);
    else { Serial1.rx = "$GPRMC,partial"; GpsReceiver::update(); }
    // No diagnostic output during the timing window, including the RX reader.
    if (Serial.used != before) return 51;
    GpsReceiver::update();
    const auto& w = backupTrace.windows[unsigned(GpsPower::BackupTest::Phase::Listen)];
    if (backupTest.result() != GpsPower::BackupTest::Result::EntryNotConfirmed || backupWakes ||
        !w.bytes || w.valid != (c == 7 ? 1u : 0u) || w.badChecksum != (c == 9 ? 1u : 0u) || !w.stored ||
        !contains("backup entry not confirmed") || contains("GPS BACKUP TEST: PASS")) return 52;
    return backupReceiver.invalid || SPI.programs || SPI.erases || SPI.enables || SPI.violations ? 53 : 0;
  }
  if (c == 4 || c == 5) {
    const uint32_t started = millis();
    while (backupTest.running() && backupTest.current() != GpsPower::BackupTest::Phase::Fix &&
           uint32_t(millis() - started) < 60000) backupTick();
    if (backupTest.current() != GpsPower::BackupTest::Phase::Fix) return 3;
    if (c == 4) GpsProfile::invalidate("simulated profile loss after wake audit");
    else { silence = true; advanceUs(5001000); }
  }
  if (!finishBackup()) return 4;
  using Result = GpsPower::BackupTest::Result;
  if (c == 0) {
    for (unsigned cycle = 1; cycle <= 3; ++cycle) {
      if (backupTest.result() != Result::Pass || backupLatched ||
          !GpsReceiver::recordingReady() || !backupTest.wakeSucceeded() ||
          !backupTest.profileVerified() || !backupTest.baudConfirmed() ||
          backupRequests != cycle || Serial1.baud != 38400) return 10 + cycle;
      if (!contains("YES (silence confirmed)") || !contains("CONFIRMED (checksum-valid NMEA)") ||
          !backupTrace.windows[unsigned(GpsPower::BackupTest::Phase::Wake)].valid ||
          backupTrace.windows[unsigned(GpsPower::BackupTest::Phase::Listen)].bytes) return 15;
      if (cycle < 3) {
        SerialConsole::handleCommand("GPS BACKUP TEST");
        if (!backupTest.running() || backupTest.profileVerified() || !finishBackup()) return 14;
      }
    }
  } else {
    const Result expected = c == 1 || c == 6 ? Result::NoWake : c == 2 ? Result::NoValidNmea : Result::ProfileFailed;
    if (backupTest.result() != expected || !backupLatched || GpsReceiver::recordingReady() ||
        !GpsReceiver::preparationFailed() || contains("GPS BACKUP TEST: PASS")) return 20 + c;
    if ((c == 1 || c == 2 || c == 6) && backupWakes != GpsPower::kWakeAttempts) return 27;
    const unsigned before = backupRequests;
    SerialConsole::handleCommand("GPS BACKUP TEST");
    if (backupTest.running() || backupRequests != before) return 28;
    if (c == 6) {
      // Simulated external receiver recovery, not a hardware recovery recipe.
      // Exercise the real audit and latch clearing, never force the latch off.
      asleep = false; wakeEnabled = true;
      if (!GpsReceiver::prepareRecording()) return 30;
      const uint32_t recoveryAt = millis();
      while (!GpsReceiver::recordingReady() && uint32_t(millis() - recoveryAt) < 45000) backupTick();
      if (backupLatched || !GpsReceiver::recordingReady() || !GpsProfile::currentVerified()) return 31;
      clear(); SerialConsole::handleCommand("GPS BACKUP TEST");
      if (!backupTest.running() || !finishBackup() || backupTest.result() != Result::Pass ||
          !GpsReceiver::recordingReady()) return 32;
    }
  }
  return backupReceiver.invalid || SPI.programs || SPI.erases || SPI.enables || SPI.violations ? 40 : 0;
}
