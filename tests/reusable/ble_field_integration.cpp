#define TACTICPOD_BLE 1
#include "field_integration.cpp"
#include "../../src/ble_phase1.h"

struct FieldBleAdapter {
  unsigned begins = 0, stops = 0, resets = 0;
  unsigned aborts = 0;
  bool link = false, activity = false, fault = false, entrySafe = false;
  bool begin() {
    ++begins;
    entrySafe = suspended && MonotonicClock::suspended() &&
      !Tcb3Session::exclusive() && !Recorder::exclusive() && !CompactRecorder::capturing();
    return entrySafe;
  }
  void service(uint32_t) {}
  bool connected() { return link; }
  bool takeActivity() { bool v = activity; activity = false; return v; }
  bool transferActive() { return false; }
  bool takeProgress() { return false; }
  void abortTransfer() { ++aborts; Tcb3Session::bleCancel(); }
  bool failed() { return fault; }
  void stop() { ++stops; link = false; }
  bool quiescent() { return !link; }
  void reset() { ++resets; }
};
FieldBleAdapter fieldBle;
BlePhase1::Runtime<FieldBleAdapter> fieldBleRuntime;
namespace BleRuntime {
bool active() { return fieldBleRuntime.active(); }
void update(uint32_t now, const BleMode::Inputs& in) { fieldBleRuntime.update(now, in, fieldBle); }
StatusLed::Color ledColor(uint32_t now) { return fieldBleRuntime.ledColor(now); }
bool returnedFromBle() { return false; }
}

extern "C" unsigned bulkReadCase(unsigned c){
  using namespace TcbDetail;
  static uint8_t output[Tcb3::Page];
  uint32_t at=Tcb3::Page;
  dumping=c==0;
  bleReading=c!=0 && c!=11;
  if(c==2)at=Tcb3::End-Tcb3::Page;
  if(c==3)++at;
  if(c==4)at=Tcb3::End;
  if(c==5)at=0xffffff00U;
  pageOpen=c==6;owned=c==7;pending=c==8;locked=c==9;resetting=c==10;
  const bool expected=c<3;
  if(!port.readPage || resetPort.readPage)return 1;
  const bool ok=port.readPage(nullptr,at,c==12?nullptr:output);
  if(ok!=expected)return 2;
  if(SPI.pageReads!=(expected?1u:0u) || SPI.bulkReads!=SPI.pageReads)return 3;
  if(expected && memcmp(output,SPI.memory+at,Tcb3::Page))return 4;
  if(SPI.transactionActive || SPI.violations || SPI.programs || SPI.enables || SPI.erases)return 5;
  if(expected){
    const uint32_t clock=c==0?16000000:4000000;
    if(SPI.position!=260 || SPI.hz!=clock || mockPinLevels[2]!=HIGH)return 6;
    if(!read(nullptr,at,output,64) || SPI.hz!=clock || SPI.violations)return 7;
    dumping=bleReading=false;
    if(!read(nullptr,at,output,64) || SPI.hz!=1000000 || status(nullptr)!=0 ||
       SPI.hz!=1000000 || SPI.violations)return 8;
  }
  return 0;
}

extern "C" unsigned bleFieldCase(unsigned c) {
  // Run the existing real-main recording/stop path with the opt-in hooks enabled.
  if (c == 0) {
    const unsigned result = fieldCase(0);
    return result ? result : fieldBle.begins ? 100 : 0;
  }
  Serial.connected = c != 5;
  setup(); startupPending = false; waitIdle(); runMs(3500);
  if (c == 5 && !standbyPolicy.inStandby()) return 1;
  // Deliberate taps exercise the actual 50/10 ms polling and release debounce.
  mockButtonLow = true; runMs(120);
  mockButtonLow = false; runMs(150);
  mockButtonLow = true; runMs(120);
  mockButtonLow = false; runMs(100);
  if (fieldBle.begins != 1 || !fieldBle.entrySafe || !BleRuntime::active()) return 2;
  if (Tcb3Session::recording() || Tcb3Session::exclusive() || SPI.programs || SPI.erases) return 3;
  if (Tcb3Session::startFromButton()) return 4;
  Tcb3Session::handleCommand("TCB START");
  Tcb3Session::handleCommand("TCB READ");
  if (Tcb3Session::exclusive() || Tcb3Session::exporting()) return 5;
  if (c >= 6) {
    using namespace TcbDetail;
    fieldBle.link = true; runMs(20);
    if (!SPI.deepPower) return 20;
    const unsigned releases=SPI.releases,identities=SPI.jedecReads;
    if(c==7)SPI.failRelease=true;
    if(c==8)SPI.wrongId=true;
    if(c==9)SPI.stuckBusy=true;
    if(c==10)SPI.memory[0]='?';
    if(c==11)owned=true;
    if(c==12)pending=true;
    if(c==13)pageOpen=true;
    if(c>=7 && c<=9){
      if(Tcb3Session::bleBeginList() || bleReading || bleListing)return 23;
      for(unsigned i=0;i<20;++i)Tcb3Session::bleService(0);
      if(!strstr(Serial.output,c==8?"JEDEC identity failed":"wake/status failed"))return 24;
      if(c==8 && !SPI.deepPower)return 25;
      if(SPI.releases!=releases+1 || (c!=8 && SPI.jedecReads!=identities))return 47;
      return SPI.programs || SPI.enables || SPI.erases || SPI.wakeDelayViolations?26:0;
    }
    if(c>=11 && c<=13){
      if(Tcb3Session::bleBeginList() || SPI.releases!=releases || SPI.jedecReads!=identities)return 27;
      owned=pending=pageOpen=false;
      for(unsigned i=0;i<20;++i)Tcb3Session::bleService(0);
      return strstr(Serial.output,"ownership/admission conflict")?0:28;
    }
    if (!Tcb3Session::bleBeginList()) return 21;
    if (SPI.deepPower || SPI.asleepAccesses) return 22;
    if(SPI.releases!=releases+1 || SPI.jedecReads!=identities+5 || SPI.wakeDelayViolations)return 29;
    if(c==15){
      Tcb3Session::bleCancel();
      return !SPI.deepPower || bleReading || bleListing?30:0;
    }
    if(c==6)Serial.connected=false;
    if(c==10)Serial.freeSpace=0;
    const unsigned listReads=SPI.bulkReads;
    for(unsigned n=0;n<10000 && !Tcb3Session::bleListDone();++n)Tcb3Session::bleService(16);
    if(!Tcb3Session::bleListDone())return 31;
    if(SPI.bulkReads==listReads || SPI.hz!=4000000 || SPI.transactionActive || SPI.pageReads)return 49;
    if(c==10){
      if(!Tcb3Session::bleListFailed())return 32;
      if(Serial.overfilledWrites || bleDiagnosticSize==0)return 48;
      Serial.freeSpace=7;
      for(unsigned i=0;i<50;++i)Tcb3Session::bleService(0);
      if(!strstr(Serial.output,"unrecognised sector-zero state"))return 33;
      Tcb3Session::bleFinishList();
      return SPI.deepPower && !bleReading?0:34;
    }
    if(Tcb3Session::bleListFailed() || Tcb3Session::bleSessionCount()!=3)return 35;
    Tcb3Session::bleFinishList();
    if(!SPI.deepPower || bleReading || !bleListing)return 36;
    const Tcb3::ManifestEntry* entry=Tcb3Session::bleSession(0);
    if(!entry)return 37;
    const uint32_t id=entry->sessionId,epoch=entry->resetEpoch;
    if(c==14){
      owned=true;
      if(Tcb3Session::bleBeginSession(id,epoch) || !SPI.deepPower)return 38;
      owned=false;
      return 0;
    }
    for(unsigned run=0;run<(c==18?2u:1u);++run){
      usbSize=0;
      const unsigned before=SPI.releases;
      const unsigned beforePages=SPI.pageReads;
      if(!Tcb3Session::bleBeginSession(id,epoch) || SPI.deepPower || SPI.releases!=before+1)return 39;
      if(c==16){
        fieldBle.link=false;runMs(30);
        return fieldBle.aborts && SPI.deepPower && !bleReading && !bleListing?0:40;
      }
      if(c==17){
        Tcb3Session::bleFinishSession();
        return SPI.deepPower && !bleReading && !bleDownloading && bleListing?0:41;
      }
      unsigned packets=0;
      for(unsigned n=0;n<10000 && !Tcb3Session::bleTransferDone();++n){
        const unsigned reads=SPI.bulkReads;
        Tcb3Session::bleService(16);
        if(SPI.bulkReads!=reads && (SPI.hz!=4000000 || SPI.transactionActive))return 50;
        unsigned length=0;const uint8_t* packet=Tcb3Session::blePacket(length);
        if(packet){
          if(length<24 || memcmp(packet,"TPX3",4))return 42;
          if(usbSize+length>sizeof(usbBytes))return 51;
          memcpy(usbBytes+usbSize,packet,length);usbSize+=length;
          ++packets;Tcb3Session::bleReleasePacket();
        }
        if(SPI.deepPower || Tcb3Session::bleTransferFailed())return 43;
      }
      if(!packets || !Tcb3Session::bleTransferDone() || SPI.pageReads-beforePages!=entry->sectorCount*14)return 44;
      Tcb3Session::bleFinishSession();
      if(!SPI.deepPower || bleReading || bleDownloading || !bleListing)return 45;
    }
    Tcb3Session::bleCancel();
    if(SPI.programs || SPI.erases || SPI.enables || SPI.violations || SPI.wakeDelayViolations ||
       bleListing || bleReading || Serial.overfilledWrites)return 46;
    return 0;
  }
  if (c == 1 || c == 5) {
    runMs(180100);
  } else if (c == 2) {
    mockButtonLow = true; runMs(3200);
  } else {
    fieldBle.link = true; runMs(20);
    mockButtonLow = true; runMs(3200); mockButtonLow = false;
    if (fieldBle.stops || fieldBle.resets) return 6;
    runMs(180000); // Connected devices are not limited by the advertising timeout.
    if (fieldBle.stops || fieldBle.resets) return 7;
    if (c == 3) {
      fieldBle.activity = true; runMs(20);
      runMs(299000); if (fieldBle.stops) return 8;
      runMs(1100);
    } else {
      fieldBle.link = false; runMs(30);
    }
  }
  if (fieldBle.stops != 1 || fieldBle.resets != 1) return 9;
  if (Tcb3Session::recording() || SPI.programs || SPI.erases) return 10;
  return mockPinLevels[LED_RED] == LOW || mockPinLevels[LED_GREEN] == LOW ||
         mockPinLevels[LED_BLUE] == LOW ? 11 : 0;
}
