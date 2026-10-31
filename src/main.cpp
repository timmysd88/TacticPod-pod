#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <LSM6DS3.h>
#include <SPI.h>
#include "flash_write_test.h"
#include "serial_console.h"
#include "recorder.h"
#include "flash_diagnostics.h"
#include "flash_identity.h"
#include "gps_receiver.h"
#include "monotonic_clock.h"
#include "ram_observations.h"
#include "compact_recorder.h"
#include "tcb3_session.h"
#include "battery_runtime.h"

namespace {
constexpr unsigned long kLedIntervalMs = 1000;
constexpr unsigned long kReadingIntervalMs = 100;
constexpr uint8_t kFlashCsPin = 2;  // XIAO D2; SPI defaults: D8/D9/D10.
constexpr uint32_t kFlashClockHz = 1000000;
LSM6DS3 imu(I2C_MODE, 0x6A);
unsigned long lastLedMs = 0;
unsigned long lastReadingMs = 0;
unsigned long lastImuPrintMs = 0;
bool wasRecording = false;
bool imuReady = false;
int imuStatus = -1;
bool redLedIsOn = false;
bool flashReportPrinted = false;
bool flashReportRequested = false;
bool imuFailureReported = false;
FlashIdentity::Reader identityReader;
bool identityRequested=false,identityRunning=false;
unsigned identityOutput=0;
struct ImuReading {
  unsigned long timestampMs;
  float ax, ay, az;
  float gx, gy, gz;
};
ImuReading latestImuReading = {};

struct JedecId {
  uint8_t manufacturer;
  uint8_t memoryType;
  uint8_t capacity;
};

JedecId readFlashJedecId() {
  SPI.beginTransaction(SPISettings(kFlashClockHz, MSBFIRST, SPI_MODE0));
  digitalWrite(kFlashCsPin, LOW);
  SPI.transfer(0x9F);  // Read JEDEC ID; this diagnostic does not modify flash.
  JedecId id;
  id.manufacturer = SPI.transfer(0x00);
  id.memoryType = SPI.transfer(0x00);
  id.capacity = SPI.transfer(0x00);
  digitalWrite(kFlashCsPin, HIGH);
  SPI.endTransaction();
  return id;
}

void printHexByte(uint8_t value) {
  Serial.print("0x");
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

void readSfdp(uint32_t address, uint8_t* data, size_t length) {
  SPI.beginTransaction(SPISettings(kFlashClockHz, MSBFIRST, SPI_MODE0));
  digitalWrite(kFlashCsPin, LOW);
  SPI.transfer(0x5A);  // Read SFDP: 24-bit address followed by 8 dummy clocks.
  SPI.transfer(static_cast<uint8_t>(address >> 16));
  SPI.transfer(static_cast<uint8_t>(address >> 8));
  SPI.transfer(static_cast<uint8_t>(address));
  SPI.transfer(0x00);
  for (size_t i = 0; i < length; ++i) {
    data[i] = SPI.transfer(0x00);
  }
  digitalWrite(kFlashCsPin, HIGH);
  SPI.endTransaction();
}

uint32_t readLe32(const uint8_t* data) {
  return uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
         (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
}

void printUint64(uint64_t value) {
  char buffer[21];
  char* cursor = buffer + sizeof(buffer) - 1;
  *cursor = '\0';
  do {
    *--cursor = '0' + value % 10;
    value /= 10;
  } while (value);
  Serial.print(cursor);
}

void reportReadCommand(const char* protocol, bool supported, uint16_t settings) {
  Serial.print("SFDP read ");
  Serial.print(protocol);
  if (!supported) {
    Serial.println(": not advertised");
    return;
  }
  const uint8_t opcode = settings >> 8;
  if (opcode == 0 || opcode == 0xFF) {
    Serial.println(": invalid advertised opcode");
    return;
  }
  Serial.print(": opcode=");
  printHexByte(opcode);
  Serial.print(" mode_clocks=");
  Serial.print((settings >> 5) & 7);
  Serial.print(" wait_clocks=");
  Serial.println(settings & 31);
}

void reportSfdp() {
  uint8_t header[8];
  readSfdp(0, header, sizeof(header));
  Serial.print("SFDP header:");
  for (uint8_t value : header) {
    Serial.print(' ');
    printHexByte(value);
  }
  Serial.println();
  if (readLe32(header) != 0x50444653UL) {
    Serial.println("SFDP INVALID / unavailable: missing SFDP signature; capacity/commands unknown");
    return;
  }
  Serial.print("SFDP revision ");
  Serial.print(header[5]);
  Serial.print('.');
  Serial.println(header[4]);
  if (header[5] != 1 || header[7] != 0xFF) {
    Serial.println("SFDP unsupported revision/access protocol; not decoding");
    return;
  }
  // Bound reads even when an absent or faulty chip returns corrupt metadata.
  const uint16_t parameterCount = uint16_t(header[6]) + 1;
  if (parameterCount > 16) {
    Serial.println("SFDP parameter count exceeds diagnostic limit (16); not decoding");
    return;
  }
  uint32_t tableAddress = 0;
  uint8_t tableLength = 0;
  uint8_t tableMinor = 0;
  bool tableFound = false;
  for (uint16_t i = 0; i < parameterCount; ++i) {
    uint8_t parameter[8];
    readSfdp(8 + uint32_t(i) * 8, parameter, sizeof(parameter));
    // BFPT parameter ID = 0xFF00. Decode only JESD216 major version 1.
    if (parameter[0] != 0 || parameter[7] != 0xFF ||
        parameter[2] != 1 || parameter[3] < 9) {
      continue;
    }
    const uint32_t address = uint32_t(parameter[4]) |
        (uint32_t(parameter[5]) << 8) | (uint32_t(parameter[6]) << 16);
    if ((address & 3) || address < 8 + uint32_t(parameterCount) * 8 ||
        address > 0x1000000UL - uint32_t(parameter[3]) * 4) {
      continue;
    }
    if (!tableFound || parameter[1] > tableMinor ||
        (parameter[1] == tableMinor && parameter[3] > tableLength)) {
      tableAddress = address;
      tableLength = parameter[3];
      tableMinor = parameter[1];
      tableFound = true;
    }
  }
  if (!tableFound) {
    Serial.println("SFDP: no valid supported Basic Flash Parameter Table; capacity/commands unknown");
    return;
  }
  // First nine DWORDs contain density, fast-read settings and erase types.
  uint8_t raw[36];
  readSfdp(tableAddress, raw, sizeof(raw));
  uint32_t words[9];
  bool allZero = true;
  bool allFF = true;
  for (size_t i = 0; i < 9; ++i) {
    words[i] = readLe32(raw + i * 4);
    allZero = allZero && words[i] == 0;
    allFF = allFF && words[i] == 0xFFFFFFFFUL;
  }
  if (allZero || allFF) {
    Serial.println("SFDP INVALID: empty/unresponsive Basic Flash Parameter Table");
    return;
  }
  const uint32_t density = words[1];
  uint64_t bits;
  if (density & 0x80000000UL) {
    const uint32_t exponent = density & 0x7FFFFFFFUL;
    if (exponent < 3 || exponent > 63) {
      Serial.println("SFDP INVALID: density exponent outside decoder range");
      return;
    }
    bits = uint64_t(1) << exponent;
  } else {
    bits = uint64_t(density) + 1;
  }
  if (bits < 8 || bits % 8 != 0) {
    Serial.println("SFDP INVALID: density is not a whole number of bytes");
    return;
  }
  Serial.print("SFDP BFPT valid, revision 1.");
  Serial.print(tableMinor);
  Serial.print(" | advertised capacity: ");
  printUint64(bits);
  Serial.print(" bits / ");
  printUint64(bits / 8);
  Serial.print(" bytes / ");
  Serial.print(double(bits) / 1048576.0, 3);
  Serial.print(" Mibit / ");
  Serial.print(double(bits) / 8388608.0, 3);
  Serial.println(" MiB");
  Serial.println("SFDP advertised commands (reported only; NOT executed):");
  reportReadCommand("1-1-2", words[0] & (1UL << 16), words[3] & 0xFFFF);
  reportReadCommand("1-2-2", words[0] & (1UL << 20), words[3] >> 16);
  reportReadCommand("1-1-4", words[0] & (1UL << 22), words[2] >> 16);
  reportReadCommand("1-4-4", words[0] & (1UL << 21), words[2] & 0xFFFF);
  reportReadCommand("2-2-2", words[4] & 1, words[5] >> 16);
  reportReadCommand("4-4-4", words[4] & (1UL << 4), words[6] >> 16);
  for (uint8_t i = 0; i < 4; ++i) {
    const uint16_t descriptor = words[7 + i / 2] >> (16 * (i % 2));
    const uint8_t exponent = descriptor & 0xFF;
    const uint8_t opcode = descriptor >> 8;
    Serial.print("SFDP erase type ");
    Serial.print(i + 1);
    if (exponent == 0) {
      Serial.println(": not advertised");
    } else if (exponent > 31 || opcode == 0 || opcode == 0xFF ||
               (uint64_t(1) << exponent) > bits / 8) {
      Serial.println(": invalid descriptor");
    } else {
      Serial.print(": opcode=");
      printHexByte(opcode);
      Serial.print(" size_bytes=");
      Serial.print(uint32_t(1) << exponent);
      Serial.println(" (NOT executed)");
    }
  }
  Serial.println("SFDP summary covers BFPT read/erase descriptors, not every device command or optional table.");
}

void reportFlashId() {
  JedecId first = {};
  bool consistent = true;
  for (uint8_t i = 0; i < 5; ++i) {
    const JedecId id = readFlashJedecId();
    if (i == 0) {
      first = id;
    }
    consistent = consistent && id.manufacturer == first.manufacturer &&
                 id.memoryType == first.memoryType && id.capacity == first.capacity;
    Serial.print("FLASH JEDEC read ");
    Serial.print(i + 1);
    Serial.print("/5 | manufacturer=");
    printHexByte(id.manufacturer);
    Serial.print(" memory_type=");
    printHexByte(id.memoryType);
    Serial.print(" capacity_id=");
    printHexByte(id.capacity);
    Serial.println();
  }
  Serial.println(consistent ? "JEDEC consistency: PASS (5/5 identical)" :
                             "JEDEC consistency: FAIL (unstable SPI response)");
  if (consistent && first.manufacturer == 0x0B &&
      first.memoryType == 0x40 && first.capacity == 0x18) {
    Serial.println("JEDEC 0B 40 18: possible XTX XT25F128B/F family; exact model/revision not confirmed by ID alone");
  } else if (consistent &&
             ((first.manufacturer == 0xFF && first.memoryType == 0xFF && first.capacity == 0xFF) ||
              (first.manufacturer == 0 && first.memoryType == 0 && first.capacity == 0))) {
    Serial.println("Flash NOT DETECTED: uniform response is not a valid chip identity");
  }
  reportSfdp();
}

// Fixed read-only allowlist; no caller can request WREN, configuration or array
// mutation. SFDP is bounded to its 256-byte register even for malformed headers.
bool identityRead(void*,uint8_t command,uint32_t address,uint8_t* out,unsigned n){
  if(!n || n>16 || !out)return false;
  if(command!=5 && command!=0x35 && command!=0x9f && command!=0x90 && command!=0x5a)return false;
  if(command==0x5a && (address>=256 || n>256-address))return false;
  if(command!=0x5a && address!=0)return false;
  SPI.beginTransaction(SPISettings(kFlashClockHz,MSBFIRST,SPI_MODE0));
  digitalWrite(kFlashCsPin,LOW);SPI.transfer(command);
  if(command==0x5a || command==0x90){
    SPI.transfer(uint8_t(address>>16));SPI.transfer(uint8_t(address>>8));SPI.transfer(uint8_t(address));
    if(command==0x5a)SPI.transfer(0); // Exactly eight dummy clocks, not an extra address byte.
  }
  for(unsigned i=0;i<n;++i)out[i]=SPI.transfer(0);
  digitalWrite(kFlashCsPin,HIGH);SPI.endTransaction();return true;
}
bool identityIdle(){
  return FlashWriteTest::diagnosticsAllowed() && !Recorder::exclusive() && !Tcb3Session::exclusive() &&
    !GpsReceiver::uartScanBusy() && !RamObservations::fastImu() && !CompactRecorder::capturing();
}
void updateIdentity(){
  if(!identityRunning)return;
  if(!Serial || !identityIdle()){
    identityReader.cancel();identityRunning=false;return; // Never inject text into an export.
  }
  if(!identityReader.done()){
    identityReader.step({nullptr,identityRead});return;
  }
  // A whole short line is queued only when USB has room: no blocking print loop.
  char line[112];unsigned size=0;
  auto append=[&](const char* p){while(*p && size+1<sizeof(line))line[size++]=*p++;};
  auto hex=[&](uint8_t b){const char* digits="0123456789ABCDEF";line[size++]=digits[b>>4];line[size++]=digits[b&15];};
  const auto& s=identityReader.data;
  if(identityReader.error){append("FLASH ID ERROR ");append(identityReader.error);}
  else if(identityOutput==0)append("BEGIN_FLASH_ID v1 SPI=1000000 MODE=0 read_only=YES SFDP_bytes=256 passes=2");
  else if(identityOutput<=5){append("FLASH ID JEDEC ");hex(identityOutput);append(" ");for(auto b:s.jedec[identityOutput-1])hex(b);}
  else if(identityOutput==6){append("FLASH ID SR1 before=");hex(s.sr1Before);append(" after=");hex(s.sr1After);append(" SR2=");hex(s.sr2);}
  else if(identityOutput==7){append("FLASH ID REMS address=000000 ");for(auto b:s.rems)hex(b);}
  else if(identityOutput<40){unsigned k=identityOutput-8;append("FLASH ID SFDP pass=");hex(k/16+1);append(" address=00");hex((k%16)*16);append(" ");for(unsigned i=0;i<16;++i)hex(s.sfdp[k/16][(k%16)*16+i]);}
  else {
    bool consistent=true;for(unsigned i=1;i<5;++i)consistent&=memcmp(s.jedec[0],s.jedec[i],3)==0;
    append("END_FLASH_ID JEDEC_repeat=");append(consistent?"PASS":"FAIL");
    append(" SFDP_repeat=");append(memcmp(s.sfdp[0],s.sfdp[1],256)==0?"PASS":"FAIL");
    append(" qualification=UNRESOLVED");
  }
  line[size++]='\n';
  if(Serial.availableForWrite()<int(size))return;
  Serial.write(reinterpret_cast<const uint8_t*>(line),size);
  if(identityReader.error || identityOutput==40)identityRunning=false;else ++identityOutput;
}
}

namespace FlashDiagnostics {
void request() {
  if (!FlashWriteTest::diagnosticsAllowed() || Recorder::exclusive() || Tcb3Session::exclusive()) {
    Serial.println("DIAG refused: flash is scanning, busy or faulted; try again when idle.");
    return;
  }
  flashReportRequested = true;
}
void requestIdentity(){
  if(Recorder::exporting() || Tcb3Session::exporting())return; // Keep CSV/binary streams clean.
  if(identityRequested || identityRunning || !identityIdle()){
    Serial.println("DIAG ID refused: require idle flash, GPS diagnostics, OBS and COMP.");return;
  }
  identityRequested=true;
}
}

void setup() {
  BatteryRuntime::begin(); // Separate reusable build only; no battery presumed.
  MonotonicClock::begin();
  RamObservations::begin();
  // The XIAO's common-anode RGB LED is active LOW.
  // Use the Seeed variant's pin definitions, not raw nRF GPIO numbers.
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  Serial.begin(115200);  // USB CDC serial; do not wait for a host connection.
  GpsReceiver::begin();
  digitalWrite(kFlashCsPin, HIGH);
  pinMode(kFlashCsPin, OUTPUT);
  SPI.begin();  // Board variant maps SCK=D8, MISO=D9, MOSI=D10.
  // Seeed's library enables IMU power and selects Wire1 via the build flag.
  imu.settings.accelEnabled = 1;
  imu.settings.gyroEnabled = 1;
  imu.settings.accelRange = 2;       // +/-2 g
  imu.settings.gyroRange = 245;      // Library's setting for +/-245 dps
  imu.settings.accelSampleRate = 104;
  imu.settings.gyroSampleRate = 104;
  imuStatus = imu.begin();
  imuReady = (imuStatus == 0);
  Recorder::begin(imuReady);  // Read-only recovery scan; recording stays OFF.
  Tcb3Session::begin(imuReady);
}

void loop() {
  (void)MonotonicClock::now(); // Boot clock continues while all recorders are idle.
  GpsReceiver::update();
  FlashWriteTest::update(); // Keep the USB command reader alive during TCB capture.
  Recorder::update();
  RamObservations::update();
  if(identityRequested){
    identityRequested=false;
    if(identityIdle() && Serial){identityReader.begin();identityOutput=0;identityRunning=true;}
  }
  updateIdentity();
  const unsigned long now = millis();
  if (now - lastLedMs >= kLedIntervalMs) {
    lastLedMs = now;
    redLedIsOn = !redLedIsOn;
    digitalWrite(LED_RED, redLedIsOn ? LOW : HIGH);

  }
  if (!Serial) {
    imuFailureReported = false;
  } else if (!identityRunning && FlashWriteTest::diagnosticsAllowed() && !Recorder::exclusive() && !Tcb3Session::exclusive() && !GpsReceiver::uartScanBusy() &&
             (!flashReportPrinted || flashReportRequested)) {
    reportFlashId();
    flashReportPrinted = true;
    flashReportRequested = false;
  }
  const bool recording = Recorder::recording();
  const bool highRate = recording || RamObservations::fastImu();
  const unsigned long sampleIntervalMs = highRate ? 10 : kReadingIntervalMs;
  if (highRate != wasRecording) {
    lastReadingMs = now - sampleIntervalMs;
    wasRecording = highRate;
  }
  if (now - lastReadingMs < sampleIntervalMs) {
    // Compression only uses foreground slack; leave >=1ms for acquisition.
    CompactRecorder::update(sampleIntervalMs - (now - lastReadingMs) > 1);
    Tcb3Session::update(sampleIntervalMs - (now - lastReadingMs) > 2);
    return;
  }
  if (recording && now - lastReadingMs >= 2 * sampleIntervalMs)
    Recorder::noteMissed((now - lastReadingMs) / sampleIntervalMs - 1);
  if (highRate && now - lastReadingMs >= 2 * sampleIntervalMs)
    Tcb3Session::noteMissed((now - lastReadingMs) / sampleIntervalMs - 1);
  lastReadingMs = now;
  if (!imuReady) {
    // Show failures once per serial connection, even with streaming disabled.
    if (Serial && !imuFailureReported) {
      Serial.print("ERROR: LSM6DS3TR-C initialisation failed, status=");
      Serial.print(imuStatus);
      Serial.println(". Check Sense board / IMU power / Wire1 address 0x6A.");
      imuFailureReported = true;
    }
    CompactRecorder::update();
    Tcb3Session::update();
    return;
  }

  // Read raw axes once: binary recording preserves the sensor's native precision.
  const uint64_t acquisitionUs = MonotonicClock::now();
  const int16_t raw[6] = {imu.readRawAccelX(), imu.readRawAccelY(), imu.readRawAccelZ(),
                          imu.readRawGyroX(), imu.readRawGyroY(), imu.readRawGyroZ()};
  RamObservations::addImu(acquisitionUs, raw);
  latestImuReading = {now, imu.calcAccel(raw[0]), imu.calcAccel(raw[1]), imu.calcAccel(raw[2]),
                     imu.calcGyro(raw[3]), imu.calcGyro(raw[4]), imu.calcGyro(raw[5])};
  Recorder::addSample(now, raw);
  CompactRecorder::update(); // Acquisition and the unchanged V1 enqueue ran first.
  Tcb3Session::update();
  if (!Serial || !SerialConsole::imuPrintingEnabled() || Recorder::exporting() ||
      now - lastImuPrintMs < kReadingIntervalMs) {
    return;
  }
  lastImuPrintMs = now;
  Serial.print("t_ms=");
  Serial.print(latestImuReading.timestampMs);
  Serial.print(" | accel_g X=");
  Serial.print(latestImuReading.ax, 4);
  Serial.print(" Y=");
  Serial.print(latestImuReading.ay, 4);
  Serial.print(" Z=");
  Serial.print(latestImuReading.az, 4);
  Serial.print(" | gyro_dps X=");
  Serial.print(latestImuReading.gx, 3);
  Serial.print(" Y=");
  Serial.print(latestImuReading.gy, 3);
  Serial.print(" Z=");
  Serial.println(latestImuReading.gz, 3);
}
