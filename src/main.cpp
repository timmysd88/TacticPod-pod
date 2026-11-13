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
#include "physical_button.h"
#include "status_led.h"
#include "standby_policy.h"
#include "flash_power.h"
#include "field_trace.h"
#include "imu_config.h"
#if TACTICPOD_BLE
#include "ble_gesture.h"
#include "ble_runtime.h"
#endif

namespace {
#if !TACTICPOD_REUSABLE
constexpr unsigned long kLedIntervalMs = 1000;
#endif
constexpr unsigned long kReadingIntervalMs = 100;
constexpr uint8_t kFlashCsPin = 2;  // XIAO D2; SPI defaults: D8/D9/D10.
constexpr uint32_t kFlashClockHz = 1000000;
LSM6DS3 imu(I2C_MODE, 0x6A);
#if !TACTICPOD_REUSABLE
unsigned long lastLedMs = 0;
#endif
unsigned long lastReadingMs = 0;
unsigned long lastImuPrintMs = 0;
bool wasRecording = false;
bool imuReady = false;
int imuStatus = -1;
#if !TACTICPOD_REUSABLE
bool redLedIsOn = false;
#endif
bool flashReportPrinted = false;
bool flashReportRequested = false;
bool imuFailureReported = false;
FlashIdentity::Reader identityReader;
bool identityRequested=false,identityRunning=false;
unsigned identityOutput=0;
int beginImu() {
  imu.settings.accelEnabled = 1;
  imu.settings.gyroEnabled = 1;
  imu.settings.accelRange = ImuConfig::AccelRangeG;
  imu.settings.gyroRange = ImuConfig::GyroRangeDps;
  imu.settings.accelSampleRate = ImuConfig::SensorRateHz;
  imu.settings.gyroSampleRate = ImuConfig::SensorRateHz;
  const int status = imu.begin();
  if (status != IMU_SUCCESS) return status;

  uint8_t ctrl1Xl = 0, ctrl2G = 0;
  if (imu.readRegister(&ctrl1Xl, 0x10) != IMU_SUCCESS ||
      imu.readRegister(&ctrl2G, 0x11) != IMU_SUCCESS ||
      !ImuConfig::fullScaleRegistersMatch(ctrl1Xl, ctrl2G)) {
    return IMU_HW_ERROR;
  }
  return IMU_SUCCESS;
}
#if TACTICPOD_REUSABLE
static_assert(PIN_A0 == 0, "Record button must use XIAO D0/P0.02");
PhysicalButton::StateMachine physicalButton;
StatusLed::Scheduler userLed;
bool lastButtonPressed=false, lastStopArmed=false;

void setUserLed(StatusLed::Color color) {
  const bool red = color == StatusLed::Color::Red;
  const bool green = color == StatusLed::Color::Green;
  const bool blue = color == StatusLed::Color::Blue;
  digitalWrite(LED_RED, red ? LOW : HIGH);
  digitalWrite(LED_GREEN, green ? LOW : HIGH);
  digitalWrite(LED_BLUE, blue ? LOW : HIGH);
}

Standby::Policy standbyPolicy;
bool startAttempt = false;
#if TACTICPOD_BLE
BleGesture::Classifier bleGesture;
#endif

struct FlashIo {
  void begin() { SPI.beginTransaction(SPISettings(kFlashClockHz, MSBFIRST, SPI_MODE0)); digitalWrite(kFlashCsPin, LOW); }
  void end() { digitalWrite(kFlashCsPin, HIGH); SPI.endTransaction(); }
  uint8_t status() { begin(); SPI.transfer(0x05); const uint8_t value = SPI.transfer(0x00); end(); return value; }
  void command(uint8_t opcode) { begin(); SPI.transfer(opcode); end(); }
  void waitUs(unsigned us) { delayMicroseconds(us); }
};

// Standby leaves D0, P0.14 (VBAT_ENABLE, LOW) and P0.13 (charge current, input) untouched.
bool enterStandby() {
  FlashIo flash;
  if (!FlashPower::deepPowerDown(flash)) return false;  // Flash busy: stay awake, retry later.
  if (imuReady) {
    imu.writeRegister(0x10, 0);  // CTRL1_XL: accelerometer power-down.
    imu.writeRegister(0x11, 0);  // CTRL2_G: gyroscope power-down.
  }
  GpsReceiver::suspend();
  MonotonicClock::suspend();
  setUserLed(StatusLed::Color::Off);
  return true;
}

void leaveStandby(uint32_t now) {
  FieldTrace::note(FieldTrace::Kind::Wake,now);
  MonotonicClock::resume();
  FlashIo flash;
  FlashPower::release(flash);
  GpsReceiver::resume();
  const bool wasReady = imuReady;
  imuStatus = beginImu();
  imuReady = imuStatus == 0;
  if (imuReady != wasReady) {
    Recorder::begin(imuReady);
    Tcb3Session::begin(imuReady);
  }
  lastReadingMs = millis();
  standbyPolicy.wake(now);
}

void updateFieldControls() {
  const uint32_t now = millis();
  const bool pressed = digitalRead(PIN_A0) == LOW;
  const PhysicalButton::Action action = physicalButton.update(
    pressed, now, Tcb3Session::recording(), Tcb3Session::exclusive());
  if(physicalButton.pressed()!=lastButtonPressed) {
    lastButtonPressed=physicalButton.pressed();
    FieldTrace::note(lastButtonPressed?FieldTrace::Kind::Press:FieldTrace::Kind::Release,now);
  }
  if(physicalButton.stopArmed() && !lastStopArmed)FieldTrace::note(FieldTrace::Kind::StopArmed,now);
  lastStopArmed=physicalButton.stopArmed();
#if TACTICPOD_BLE
  BleMode::Inputs ble;
  ble.recording = Tcb3Session::recording() || Recorder::recording();
  ble.sessionBusy = Tcb3Session::exclusive() || Recorder::exclusive() ||
    CompactRecorder::capturing() || RamObservations::capturing() || RamObservations::fastImu() ||
    GpsReceiver::busy() || identityRequested || identityRunning ||
    !FlashWriteTest::diagnosticsAllowed() || startAttempt;
  ble.faulted = Tcb3Session::faulted();
  ble.startHold = action == PhysicalButton::Action::Start;
  const bool eligible = !ble.recording && !ble.sessionBusy && !ble.faulted && !BleRuntime::active();
  ble.doubleTap = bleGesture.update(pressed, now, eligible) == BleGesture::Event::DoubleTap;
  if (ble.doubleTap && !standbyPolicy.inStandby() && !enterStandby()) {
    ble.doubleTap = false;
    Serial.println("BLE refused: external flash is not quiescent.");
    userLed.notify(StatusLed::Event::StartFailed, now);
  }
  BleRuntime::update(now, ble);
  if (BleRuntime::active()) {
    setUserLed(BleRuntime::ledColor(millis()));
    return; // In particular, never forward an advertising-cancel hold to recording.
  }
#endif
  if (action == PhysicalButton::Action::Start) {
    // Peripherals are awake and the flash is released BEFORE the recorder is asked to start.
    if (standbyPolicy.inStandby()) leaveStandby(now);
    if (Tcb3Session::startFromButton()) {
      startAttempt = true;
      FieldTrace::note(FieldTrace::Kind::StartAccepted,now);
    } else {
      userLed.notify(StatusLed::Event::StartFailed, now);
    }
  } else if (action == PhysicalButton::Action::Stop) {
    FieldTrace::note(FieldTrace::Kind::StopAccepted,now);
    Tcb3Session::handleCommand("TCB STOP");
  }

  const bool recording = Tcb3Session::recording();
  if (startAttempt) {
    if (recording || Tcb3Session::faulted()) {
      startAttempt = false;
    } else if (!Tcb3Session::exclusive()) {  // Scan/admission refused after acceptance.
      startAttempt = false;
      userLed.notify(StatusLed::Event::StartFailed, now);
    }
  }

  Standby::Inputs in;
  in.recording = recording;
  in.sessionBusy = Tcb3Session::exclusive();
  in.faulted = Tcb3Session::faulted();
  in.usbPresent = BatteryRuntime::usbPresent();
  in.serialOpen = bool(Serial);
  in.gpsBusy = GpsReceiver::busy();
  in.captureActive = CompactRecorder::capturing() || RamObservations::capturing() ||
                     RamObservations::fastImu() || Recorder::exclusive();
  in.buttonBusy = physicalButton.busy();
  in.ledBusy = userLed.eventActive();
  in.startAttempt = startAttempt;
  const Standby::Policy::Change change = standbyPolicy.update(in, now);
  if (change == Standby::Policy::Change::Enter) {
    if (!enterStandby()) standbyPolicy.wake(now);
    else FieldTrace::note(FieldTrace::Kind::StandbyEnter,now);
  } else if (change == Standby::Policy::Change::Exit) {
    leaveStandby(now);
  }
  if (standbyPolicy.inStandby()) {
    setUserLed(StatusLed::Color::Off);
    return;
  }

  setUserLed(userLed.update(now, Tcb3Session::faulted(), recording));
}

// Requested explicitly while stopped; one bounded line per foreground slice.
void flushFieldTrace() {
  auto& b=FieldTrace::buffer();
  if(!b.remaining || !b.count || !Serial || Tcb3Session::exclusive() || Recorder::exclusive() || GpsReceiver::busy())return;
  const auto& e=b.events[b.head];char line[150];
  const int n=FieldTrace::format(e,b.overwritten,line,sizeof(line));
  if(n<=0 || n>=int(sizeof(line)) || Serial.availableForWrite()<n)return;
  if(Serial.write(reinterpret_cast<const uint8_t*>(line),n)!=unsigned(n))return;
  b.head=(b.head+1)%64;--b.count;--b.remaining;
}

// Standby pace: no GPS/IMU/flash/LED work. D0 is polled; sleeping is the RTOS tickless idle.
void standbyIdle() {
  if (BatteryRuntime::monitor().s.cell) BatteryRuntime::update(true);  // Keeps armed-cell trust warm.
#if TACTICPOD_BLE
  delay(bleGesture.wantsFastPoll() || digitalRead(PIN_A0) == LOW ? 10 : 50);
#else
  delay(digitalRead(PIN_A0) == LOW ? 10 : 50);
#endif
}
#endif

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
  return FlashWriteTest::diagnosticsAllowed() && !Recorder::exclusive() && !Tcb3Session::busy() &&
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
#if TACTICPOD_REUSABLE
  pinMode(PIN_A0, INPUT_PULLUP);
#endif

  Serial.begin(115200);  // USB CDC serial; do not wait for a host connection.
  GpsReceiver::begin();
  digitalWrite(kFlashCsPin, HIGH);
  pinMode(kFlashCsPin, OUTPUT);
  SPI.begin();  // Board variant maps SCK=D8, MISO=D9, MOSI=D10.
#if TACTICPOD_REUSABLE
  {
    // A reset during standby leaves the flash in deep power-down; always release it first.
    FlashIo flash;
    FlashPower::release(flash);
  }
#endif
  // Seeed's library enables IMU power and selects Wire1 via the build flag.
  imuStatus = beginImu();
  imuReady = (imuStatus == 0);
  Recorder::begin(imuReady);  // Read-only recovery scan; recording stays OFF.
  Tcb3Session::begin(imuReady);
#if TACTICPOD_REUSABLE
  // Startup indication only after a successful startup: IMU ready and no recorder fault.
  const bool showPowerOn =
#if TACTICPOD_BLE
    !BleRuntime::returnedFromBle();
#else
    true;
#endif
  if (showPowerOn && imuReady && !Tcb3Session::faulted()) userLed.notify(StatusLed::Event::PowerOn, millis());
#endif
}

void loop() {
#if TACTICPOD_BLE
  if (BleRuntime::active()) {
    updateFieldControls();
    if (BatteryRuntime::monitor().s.cell) BatteryRuntime::update(true);
    delay(10);
    return; // No USB commands, IMU/GPS service, recorder updates or flash access in BLE mode.
  }
#endif
  (void)MonotonicClock::now(); // Boot clock continues while all recorders are idle.
  GpsReceiver::update();
#if TACTICPOD_REUSABLE
  updateFieldControls();
#if TACTICPOD_BLE
  if (BleRuntime::active()) return;
#endif
  flushFieldTrace();
  if (standbyPolicy.inStandby()) {
    standbyIdle();
    return;
  }
#endif
  FlashWriteTest::update(); // Keep the USB command reader alive during TCB capture.
  Recorder::update();
  RamObservations::update();
  if(identityRequested){
    identityRequested=false;
    if(identityIdle() && Serial){identityReader.begin();identityOutput=0;identityRunning=true;}
  }
  updateIdentity();
  const unsigned long now = millis();
#if !TACTICPOD_REUSABLE
  if (now - lastLedMs >= kLedIntervalMs) {
    lastLedMs = now;
    redLedIsOn = !redLedIsOn;
    digitalWrite(LED_RED, redLedIsOn ? LOW : HIGH);

  }
#endif
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
#if TACTICPOD_BLE && TACTICPOD_REUSABLE
  if(!identityRunning && !identityRequested && now-lastReadingMs<sampleIntervalMs &&
     sampleIntervalMs-(now-lastReadingMs)>2)Tcb3Session::bleIdleService();
#endif
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
  latestImuReading = {now, ImuConfig::accelG(raw[0]), ImuConfig::accelG(raw[1]),
                     ImuConfig::accelG(raw[2]), ImuConfig::gyroDps(raw[3]),
                     ImuConfig::gyroDps(raw[4]), ImuConfig::gyroDps(raw[5])};
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
