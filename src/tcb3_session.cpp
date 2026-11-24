#include "tcb3_session.h"
#include "device_identity.h"
#if TACTICPOD_TCB3_FLASH_ENABLED
#include "tcb3_store.h"
#include "compact_recorder.h"
#include "monotonic_clock.h"
#include "recorder.h"
#include "flash_write_test.h"
#include "gps_receiver.h"
#include "ram_observations.h"
#include "battery_runtime.h"
#include <SPI.h>
#include "field_trace.h"
#if TACTICPOD_BLE
#include "ble_runtime.h"
#include "flash_power.h"
#include "ble_performance.h"
#include "tcb3_fast_discovery.h"
#include <new>
#endif
namespace {
namespace TcbDetail {
Tcb3::Store store;Tcb3::Export dump;
#if TACTICPOD_BLE && TACTICPOD_REUSABLE
union BleCacheStorage {
  Tcb3::Manifest manifest;
  Tcb3FastDiscovery::Scanner scanner;
  BleCacheStorage():manifest(){}
  ~BleCacheStorage()=default;
} bleCache;
Tcb3::Export bleDump;
enum class ManifestCache:uint8_t {Cold,Scanning,Ready,Failed};
ManifestCache manifestCache=ManifestCache::Cold;
bool bleReading=false,bleListing=false,bleDownloading=false;
bool provisionalMode=false,scannerActive=false,validating=false,validationDone=false,validationFailed=false;
Tcb3::ManifestEntry selectedProvisional{},provisionalEntry{};
void useManifest(){
  if(scannerActive){
    bleCache.scanner.~Scanner();
    new (&bleCache.manifest) Tcb3::Manifest;
    scannerActive=false;
  }
}
void useScanner(){
  if(!scannerActive){
    bleCache.manifest.~Manifest();
    new (&bleCache.scanner) Tcb3FastDiscovery::Scanner;
    scannerActive=true;
  }
}
char bleDiagnostic[256]{};
unsigned bleDiagnosticSize=0,bleDiagnosticAt=0,bleDiagnosticDropped=0;
#endif
bool storeImuReady=false,pending=false,owned=false,dumping=false,locked=false,finishing=false;
uint64_t offered[4]{};uint64_t stopUs=0;
uint32_t storeMissed=0,maxServiceUs=0;
unsigned exportSent=0;
Tcb3::Reason reason=Tcb3::Reason::Clean;
bool cleanCompletionPending=false;
char line[768]{};
unsigned lineSize=0,lineAt=0;
// Large status lines are sent in whatever USB room exists so they can never wedge the console.
void flushLine(){
  if(!lineSize || !Serial)return;
  int room=Serial.availableForWrite();if(room<=0)return;
  unsigned n=lineSize-lineAt;if(n>unsigned(room))n=unsigned(room);
  Serial.write(reinterpret_cast<const uint8_t*>(line+lineAt),n);lineAt+=n;
  if(lineAt>=lineSize)lineSize=lineAt=0;
}
bool pageOpen=false;unsigned pageSent=0;
uint64_t testStartedUs=0;
bool testUsed=false;
#if TACTICPOD_REUSABLE
uint32_t sessionSeconds=120;
bool batteryWarningRecorded=false;
// BAT STATUS is queued as short lines: the USB CDC TX FIFO (256 bytes) cannot take it in one write.
BatteryRuntime::Report batReport;unsigned batIndex=0;bool batActive=false;
// Until-stop sessions (TCB DURATION UNLIMITED, or a physical-button start) have no time cap.
// The store keeps page 15 of every sector for the footer, so a full partition still finalises.
bool configuredUnlimited=false,buttonRequest=false,sessionUnlimited=false;
bool waitingGps=false;
uint32_t gpsWaitAt=0;
constexpr uint32_t UnlimitedMinSectors=8; // Refuse to begin an until-stop session in a nearly full partition.
uint64_t captureLimit(){return uint64_t(sessionSeconds)*1000000;}
bool timeLimited(){return !sessionUnlimited;}
bool capReached(uint64_t now){return timeLimited() && now-testStartedUs>=captureLimit();}
uint64_t deadline(){return finishing?stopUs+60000000:timeLimited()?testStartedUs+captureLimit()+60000000:~0ULL;}
bool resetting=false,resetArmed=false,resetFailed=false;uint32_t resetArmedAt=0;
bool mutationBlocked(){return owned || pending || dumping || locked || resetting || Recorder::exclusive();}
#endif
constexpr uint32_t kUsbExportReadHz=16000000; // Experimental; restore 4000000 for the verified USB baseline.
void select(uint32_t hz=1000000){SPI.beginTransaction(SPISettings(hz,MSBFIRST,SPI_MODE0));digitalWrite(2,LOW);}
void deselect(){digitalWrite(2,HIGH);SPI.endTransaction();}
void address(uint32_t a){SPI.transfer(a>>16);SPI.transfer(a>>8);SPI.transfer(a);}
int status(void*){if(pageOpen)return -1;select();SPI.transfer(5);int s=SPI.transfer(0);deselect();return s;}
bool exportReadActive(){
  bool fast=dumping;
#if TACTICPOD_BLE && TACTICPOD_REUSABLE
  fast=fast||bleReading;
#endif
  return fast;
}
uint32_t readClockHz(){
#if TACTICPOD_BLE && TACTICPOD_REUSABLE
  if(bleReading)return 4000000;
#endif
  return dumping?kUsbExportReadHz:1000000;
}
bool read(void*,uint32_t a,uint8_t* p,unsigned n){
  if(pageOpen || !n || n>64 || a>=Tcb3::End || n>Tcb3::End-a)return false;
#if TACTICPOD_BLE && TACTICPOD_REUSABLE
  BlePerformance::FlashRead timing(bleReading || BlePerformance::counters().cacheStepActive,n);
#endif
  static uint8_t txBuffer[64]{}; // EasyDMA requires RAM, not a const buffer in flash.
  select(readClockHz());SPI.transfer(3);address(a);SPI.transfer(txBuffer,p,n);deselect();return true;
}
bool readPage(void*,uint32_t a,uint8_t* p){
  if(pageOpen || !exportReadActive() || owned || pending || locked || !p ||
     (a&(Tcb3::Page-1)) || a>=Tcb3::End || Tcb3::Page>Tcb3::End-a)return false;
#if TACTICPOD_REUSABLE
  if(resetting)return false;
#endif
#if TACTICPOD_BLE && TACTICPOD_REUSABLE
  BlePerformance::FlashRead timing(bleReading,Tcb3::Page);
#endif
  static uint8_t txBuffer[Tcb3::Page]{}; // Mutable RAM: one zero-filled EasyDMA payload.
  select(readClockHz());SPI.transfer(3);address(a);SPI.transfer(txBuffer,p,Tcb3::Page);deselect();return true;
}
bool beginPage(void*,uint32_t a){
  if(pageOpen || (a&255) || a<store.floor() || a>=Tcb3Test::WriteEnd || 256>Tcb3Test::WriteEnd-a)return false;
  if(Tcb3Test::Append && !store.stats.programOperations &&
     (a!=Tcb3Test::WriteStart || store.stats.session!=Tcb3Test::ExpectedSession || store.stats.sectorSequence!=0 ||
      store.stats.next!=Tcb3Test::WriteStart+Tcb3::Sector))return false;
  if(Tcb3Test::Enabled && (!owned || locked || MonotonicClock::now()-testStartedUs>=Tcb3Test::FinaliseUs))return false;
#if TACTICPOD_REUSABLE
  if(!owned || locked || MonotonicClock::now()>=deadline())return false;
  if(!store.stats.programOperations && (a!=store.initialAddress() || store.stats.session!=a/Tcb3::Sector+1 ||
     store.stats.sectorSequence || store.stats.next!=a+Tcb3::Sector))return false;
#endif
  if(status(nullptr)!=0)return false;
  select();SPI.transfer(6);deselect();if(status(nullptr)!=2)return false;
  select();SPI.transfer(2);address(a);pageOpen=true;pageSent=0;return true;
}
bool pageBytes(void*,const uint8_t* p,unsigned n){
  if(!pageOpen || !n || n>64 || pageSent+n>256)return false;
  for(unsigned i=0;i<n;++i)SPI.transfer(p[i]);
  pageSent+=n;return true;
}
bool endPage(void*){if(!pageOpen)return false;bool full=pageSent==256;deselect();pageOpen=false;return full;}
void abortPage(void*){if(pageOpen){deselect();pageOpen=false;}}
#if TACTICPOD_REUSABLE
Tcb3::Reset resetEngine;
bool writeEnable(){select();SPI.transfer(6);deselect();return status(nullptr)==2;}
// Lazy reuse of a stale sector: only while a session owns the flash, never sector 0, only with a reset epoch active.
bool eraseStale(void*,uint32_t a){
  // Only the exact head sector the store is probing or preparing may be erased; live sectors lie strictly below it.
  const Tcb3::State st=store.state();
  if(pageOpen || !(owned || pending) || locked || !store.resetEpoch() || (a&4095) || a<Tcb3::Sector || a>=Tcb3Test::WriteEnd ||
     a!=store.stats.next || (st!=Tcb3::State::Probe && st!=Tcb3::State::Blank))return false;
  if(status(nullptr)!=0 || !writeEnable())return false;
  select();SPI.transfer(0x20);address(a);deselect();return true;
}
// Reset primitives touch address 0 only (the marker sector) and only while RECORD RESET is running.
bool resetErase(void*,uint32_t a){
  if(pageOpen || !resetting || a!=0)return false;
  if(status(nullptr)!=0 || !writeEnable())return false;
  select();SPI.transfer(0x20);address(0);deselect();return true;
}
bool resetProgram(void*,uint32_t a,const uint8_t* p,unsigned n){
  if(pageOpen || !resetting || a!=0 || !n || n>256)return false;
  if(status(nullptr)!=0 || !writeEnable())return false;
  select();SPI.transfer(2);address(0);for(unsigned i=0;i<n;++i)SPI.transfer(p[i]);deselect();return true;
}
Tcb3::Port resetPort{nullptr,read,beginPage,pageBytes,endPage,abortPage,status,resetProgram,resetErase};
Tcb3::Port port{nullptr,read,beginPage,pageBytes,endPage,abortPage,status,nullptr,eraseStale,readPage};
#else
Tcb3::Port port{nullptr,read,beginPage,pageBytes,endPage,abortPage,status,nullptr,nullptr,readPage};
#endif
bool storeIdentity(uint32_t* observedId=nullptr){
  for(unsigned i=0;i<5;++i){select();SPI.transfer(0x9f);unsigned a=SPI.transfer(0),b=SPI.transfer(0),c=SPI.transfer(0);deselect();
    if(observedId)*observedId=(a<<16)|(b<<8)|c;
    if(a!=11 || b!=64 || c!=24)return false;}
  if(Tcb3Test::Guarded){
    if(status(nullptr)!=0)return false;
    select();SPI.transfer(0x35);uint8_t sr2=SPI.transfer(0);deselect();
    if(sr2!=2)return false; // The measured B-compatible protection/QE state only.
  }
  return true;
}
#if TACTICPOD_BLE && TACTICPOD_REUSABLE
Tcb3::Port bleReadOnlyPort(){
  return {nullptr,read,nullptr,nullptr,nullptr,nullptr,status,nullptr,nullptr,nullptr};
}
void bleReport(const char* reason,int flashStatus=-1,uint32_t jedec=0){
  char text[144];unsigned length=0;
  auto append=[&](const char* p){while(*p && length<sizeof(text))text[length++]=*p++;};
  append("BLE flash: ");append(reason);append(" status=");
  if(flashStatus<0)append("-1");
  else{
    char digits[11];unsigned at=10;digits[at]=0;unsigned value=unsigned(flashStatus);
    do{digits[--at]=char('0'+value%10);value/=10;}while(value);
    append(digits+at);
  }
  append(" jedec=");
  for(int shift=20;shift>=0;shift-=4){
    const char digit[]={"0123456789abcdef"[(jedec>>shift)&15],0};append(digit);
  }
  append("\n");
  if(length==sizeof(text) || length>sizeof(bleDiagnostic)-bleDiagnosticSize){++bleDiagnosticDropped;return;}
  memcpy(bleDiagnostic+bleDiagnosticSize,text,length);bleDiagnosticSize+=length;
}
void bleFlushDiagnostic(){
  if(!Serial)return;
  const int room=Serial.availableForWrite();
  if(room<=0)return;
  const unsigned remaining=bleDiagnosticSize-bleDiagnosticAt;
  const unsigned n=remaining<unsigned(room)?remaining:unsigned(room);
  if(n){Serial.write(reinterpret_cast<const uint8_t*>(bleDiagnostic+bleDiagnosticAt),n);bleDiagnosticAt+=n;}
  if(bleDiagnosticAt==bleDiagnosticSize){
    bleDiagnosticAt=bleDiagnosticSize=0;
    if(bleDiagnosticDropped){
      const unsigned dropped=bleDiagnosticDropped;
      bleDiagnosticDropped=0;
      bleReport("diagnostics suppressed",int(dropped));
    }
  }
}
struct BleFlashIo {
  uint8_t status(){return uint8_t(TcbDetail::status(nullptr));}
  void command(uint8_t opcode){select();SPI.transfer(opcode);deselect();}
  void waitUs(unsigned us){delayMicroseconds(us);}
};
void bleSleepFlash(){
  if(!bleReading)return;
  BleFlashIo flash;
  if(!FlashPower::deepPowerDown(flash))bleReport("sleep refused",status(nullptr));
  bleReading=false;
}
bool bleReadAllowed(){
  if(!BleRuntime::active() || Tcb3Session::exclusive() || Tcb3Session::faulted() || pageOpen ||
     Recorder::exclusive() || CompactRecorder::capturing() || !FlashWriteTest::diagnosticsAllowed()){
    bleReport("ownership/admission conflict");return false;
  }
  return true;
}
bool bleWakeFlash(){
  BleFlashIo flash;
  FlashPower::release(flash);
  bleReading=true;
  const int s=status(nullptr);
  if(s!=0){bleReport("wake/status failed",s);bleSleepFlash();return false;}
  uint32_t id=0;
  if(!storeIdentity(&id)){bleReport("JEDEC identity failed",s,id);bleSleepFlash();return false;}
  return true;
}
bool cacheIdle(){
  return !Tcb3Session::exclusive() && !Tcb3Session::faulted() && !pageOpen &&
    !Recorder::exclusive() && !CompactRecorder::capturing() &&
    !RamObservations::capturing() && !RamObservations::fastImu() &&
    !GpsReceiver::busy() && FlashWriteTest::diagnosticsAllowed();
}
void updateManifestCache(unsigned steps,bool sleepAfter){
  if(!steps || manifestCache==ManifestCache::Ready || manifestCache==ManifestCache::Failed ||
     bleListing || bleDownloading || !cacheIdle())return;
  if(sleepAfter){
    if(!bleWakeFlash()){manifestCache=ManifestCache::Failed;BlePerformance::finishCache(true);return;}
  }else if(manifestCache==ManifestCache::Cold && !storeIdentity()){
    bleReport("cache identity/status failed");manifestCache=ManifestCache::Failed;
    BlePerformance::finishCache(true);return;
  }
  if(manifestCache==ManifestCache::Cold){
    BlePerformance::startCache();
    if(scannerActive)bleCache.scanner.begin(bleReadOnlyPort(),Tcb3Test::WriteEnd);
    else bleCache.manifest.begin(port,0,Tcb3Test::WriteEnd);
    manifestCache=ManifestCache::Scanning;
  }
  while(steps-- && !(scannerActive?bleCache.scanner.done():bleCache.manifest.done())){
    BlePerformance::CacheStep timing;
    if(scannerActive)bleCache.scanner.update(true);else bleCache.manifest.update();
  }
  if(scannerActive?bleCache.scanner.done():bleCache.manifest.done()){
    const bool failed=scannerActive?
      (bleCache.scanner.result()!=Tcb3FastDiscovery::Result::Complete &&
       bleCache.scanner.result()!=Tcb3FastDiscovery::Result::Incomplete):bleCache.manifest.failed();
    manifestCache=failed?ManifestCache::Failed:ManifestCache::Ready;
    BlePerformance::finishCache(failed);
    if(failed){
      if(scannerActive && bleCache.scanner.result()==Tcb3FastDiscovery::Result::Overflow)
        bleReport("provisional session cache overflow");
      else bleReport(scannerActive?bleCache.scanner.error():bleCache.manifest.errorMessage());
    }
  }
  if(sleepAfter)bleSleepFlash();
}
#endif
void storeReport(const char* text,bool includeBusyDetails=false){
  if(lineAt)return; // A partly sent line owns the buffer; never splice a new message into it.
  const auto& s=store.stats;uint64_t saved=0,seen=0;for(unsigned i=0;i<4;++i){saved+=s.committed[i];seen+=offered[i];}
  lineSize=0;lineAt=0;
  auto append=[](const char* p){while(*p && lineSize+1<sizeof(line))line[lineSize++]=*p++;};
  auto number=[&](uint64_t n){char b[21];unsigned k=20;b[k]=0;do{b[--k]='0'+n%10;n/=10;}while(n);append(b+k);};
  append("TCB ");append(text);append(" state=");number(unsigned(store.state()));append(" session=");number(s.session);
  append(" blocks=");number(s.blocks);append(" committed=");number(saved);append(" offered=");number(seen);
  append(" rejected=");number(CompactRecorder::stats().rejected);append(" missed=");number(storeMissed);
  append(" full=");number(s.full);append(" crc=");number(s.crcFailures);append(" incomplete=");number(s.incompleteWrites);
  append(" max_service_us=");number(maxServiceUs);append(" error=");append(store.error?store.error:"none");
  append(" programs=");number(s.programOperations);append(" max_block_ms=");number(s.maxBlockMs);
  if(includeBusyDetails){
    auto flag=[&](const char* name,bool value){append(" ");append(name);append(value?"=1":"=0");};
    flag("tcb_pending",pending);flag("tcb_owned",owned);flag("tcb_dumping",dumping);flag("tcb_locked",locked);
#if TACTICPOD_REUSABLE
    flag("tcb_resetting",resetting);
#endif
    flag("recorder_busy",Recorder::exclusive());append(" recorder_stage=");append(Recorder::diagnosticStage());
    flag("flash_diagnostic_busy",!FlashWriteTest::diagnosticsAllowed());
    append(" flash_diagnostic_state=");append(FlashWriteTest::diagnosticState());
    flag("gps_uart_scan_active",GpsReceiver::uartScanActive());
    flag("gps_configuration_active",GpsReceiver::configurationActive());
    flag("gps_profile_audit_active",GpsReceiver::profileAuditActive());
  }
  if(Tcb3Test::Enabled)append(Tcb3Test::Marker);
  if(Tcb3Test::Append){append(" skip=");number(s.probeSkipped);}
#if TACTICPOD_REUSABLE
  append(" reusable duration_s=");number(configuredUnlimited?0:sessionSeconds);append(" drain_s=60 lock=");number(locked);
  append(" mode=");append((owned||pending?sessionUnlimited:configuredUnlimited)?"until_stop":"timed");
  if(s.next>=Tcb3Test::WriteStart && s.next<=Tcb3Test::WriteEnd){append(" free_sectors=");number((Tcb3Test::WriteEnd-s.next)/Tcb3::Sector);}
#endif
  line[lineSize++]='\n';
}
void stop(Tcb3::Reason why){
  if(pending && !owned){store.cancelScan();pending=false;return;}
  if(!owned)return;
  CompactRecorder::stop();
  if(!finishing){stopUs=MonotonicClock::now();reason=why;finishing=true;
    FieldTrace::note(FieldTrace::Kind::Finalising,millis(),store.stats.session,unsigned(why));
  }else if(why!=Tcb3::Reason::Clean)reason=why;
}
#if TACTICPOD_REUSABLE
void batteryEvent(unsigned action){
  uint8_t e[32]{};e[0]=4;e[1]=1;Tcb3::put(e+2,32,2);Tcb3::put(e+4,MonotonicClock::now(),8);
  Tcb3::put(e+12,0x100,2);e[14]=action==1?1:2;Tcb3::put(e+16,action);Tcb3::put(e+20,1);
  const auto& b=BatteryRuntime::monitor().s;
  Tcb3::put(e+24,(uint64_t(b.filteredMv)<<32)|b.mv,8);++offered[3];CompactRecorder::observe(e,sizeof(e));
}
#endif
}
}
namespace Tcb3Session {
using namespace TcbDetail;
void begin(bool ready){storeImuReady=ready;
#if TACTICPOD_BLE && TACTICPOD_REUSABLE
  invalidateBleManifest();
#endif
#if TACTICPOD_REUSABLE
  BatteryRuntime::begin();RamObservations::handleCommand("OBS STOP");
#endif
}
bool exclusive(){return pending || owned || dumping || locked
#if TACTICPOD_REUSABLE
  || resetting
#endif
  ;}
bool exporting(){return dumping;}
bool flashOwned(){return pending || owned || dumping || pageOpen
#if TACTICPOD_REUSABLE
  || resetting
#endif
  ;}
// Read-only identity reads need an idle chip. Only after a fault (locked store or failed reset) is the flash
// polled, so an erase or program still running is never read through; normal idle behaviour is unchanged.
bool busy(){return pending || owned || dumping || pageOpen || (locked && status(nullptr)!=0)
#if TACTICPOD_REUSABLE
  || (resetFailed && status(nullptr)!=0)
  || resetting
#endif
  ;}
bool recording(){return owned && !finishing;}
bool faulted(){return locked || store.state()==Tcb3::State::Fault || CompactRecorder::stats().fault
#if TACTICPOD_REUSABLE
  || resetFailed
#endif
  ;}
bool takeCleanCompletion(){bool completed=cleanCompletionPending;cleanCompletionPending=false;return completed;}
void observation(unsigned kind){
  if(Tcb3Test::Enabled && owned && !finishing && MonotonicClock::now()-testStartedUs>=Tcb3Test::CaptureUs)stop(Tcb3::Reason::Clean);
#if TACTICPOD_REUSABLE
  if(owned && !finishing && capReached(MonotonicClock::now()))stop(Tcb3::Reason::Clean);
#endif
  if(owned && !finishing && kind>=1 && kind<=4 && CompactRecorder::capturing())++offered[kind-1];
}
void noteMissed(uint32_t n){if(owned && !finishing){storeMissed+=n;stop(Tcb3::Reason::Deadline);}}
bool guardCommand(const char* p){
  if(dumping)return true; // Never inject text into framed binary output.
  if(!strcmp(p,"DEVICE INFO"))return false;
  if(Tcb3Test::Guarded && (!strcmp(p,"RECORD START") || !strcmp(p,"RECORD ERASE") ||
     !strcmp(p,"RECORD ERASE CONFIRM") || !strcmp(p,"ERASE FFF000"))){
    storeReport("REFUSED: test build blocks V1 writes and all erases");return true;
  }
  if(!exclusive())return false;
#if TACTICPOD_REUSABLE
  if(!strcmp(p,"BAT STATUS"))return false;
#endif
  // A latched fault must not hide the flash identity; identityIdle() still waits for the chip to be idle.
  if(!strcmp(p,"DIAG ID") && locked && !pending && !owned && !dumping)return false;
  if(!strcmp(p,"FACTORY DIAG") && locked && !pending && !owned && !dumping)return false;
  if(!strcmp(p,"TCB STOP") || !strcmp(p,"TCB STATUS") || !strcmp(p,"COMP STATUS") ||
     !strcmp(p,"OBS STATUS") || !strcmp(p,"RECORD STATUS") || !strcmp(p,"GPS STATUS") || !strcmp(p,"IMU OFF"))return false;
  storeReport("REFUSED: flash/compressor owned");return true;
}
bool handleCommand(const char* p){
#if TACTICPOD_REUSABLE
  if(!strncmp(p,"BAT ",4)){
    if(strcmp(p,"BAT STATUS") && (mutationBlocked() || CompactRecorder::capturing())){
      storeReport("REFUSED: battery config while busy");return true;
    }
    bool ok=!strcmp(p,"BAT STATUS") || BatteryRuntime::command(p);
    if(!ok){storeReport("REFUSED: BAT command/calibration/limits");return true;}
    BatteryRuntime::capture(batReport);batIndex=0;batActive=true;return true;
  }
  if(!strncmp(p,"TCB DURATION ",13)){
    if(mutationBlocked()){storeReport("REFUSED: duration while busy");return true;}
    if(!strcmp(p+13,"UNLIMITED")){configuredUnlimited=true;storeReport("DURATION configured until stop");return true;}
    uint32_t n=0;const char* q=p+13;bool valid=*q!=0;
    while(*q){if(*q<'0' || *q>'9' || n>1800){valid=false;break;}n=n*10+(*q++-'0');}
    if(!valid || !n || n>1800){storeReport("REFUSED: duration 1..1800 seconds");return true;}
    sessionSeconds=n;configuredUnlimited=false;storeReport("DURATION configured");return true;
  }
  if(!strcmp(p,"RECORD RESET") || !strcmp(p,"RECORD RESET CONFIRM")){
    const bool confirm=p[12]!=0;
    const bool armed=resetArmed && uint32_t(millis()-resetArmedAt)<=30000;
    resetArmed=false;
    if(mutationBlocked() || CompactRecorder::capturing() || RamObservations::capturing() || Recorder::exclusive() ||
       !FlashWriteTest::diagnosticsAllowed() || GpsReceiver::uartScanBusy()){storeReport("REFUSED: reset while busy");return true;}
    if(!storeIdentity()){storeReport("REFUSED: reset JEDEC/status");return true;}
    if(!confirm){
      resetArmed=true;resetArmedAt=millis();
      Serial.println("WARNING: RECORD RESET hides ALL recordings and restarts storage allocation. Sector 0xFFF000-0xFFFFFF is untouched.");
      Serial.println("Old sectors are erased only when reused; use RECORD ERASE for a full physical erase. Not undoable through the firmware.");
      Serial.println("Within 30 seconds send RECORD RESET CONFIRM to authorise; nothing has been changed.");
      return true;
    }
    if(!armed){storeReport("REFUSED: send RECORD RESET first, then RECORD RESET CONFIRM within 30 s");return true;}
#if TACTICPOD_BLE
    invalidateBleManifest();
#endif
    resetting=true;resetFailed=false;resetEngine.begin(resetPort,millis());
    storeReport("RESET started; flash busy until complete");return true;
  }
#endif
  if(strncmp(p,"TCB ",4))return false;
  if(!strcmp(p,"TCB STATUS")){storeReport("STATUS");return true;}
  if(!strcmp(p,"TCB STOP")){stop(Tcb3::Reason::Clean);storeReport("STOP requested");return true;}
  if(strcmp(p,"TCB START") && strcmp(p,"TCB READ")){storeReport("commands: START STOP STATUS READ");return true;}
#if TACTICPOD_BLE
  if(BleRuntime::active()){storeReport("REFUSED: Bluetooth Mode active",true);return true;}
#endif
  if(exclusive() || Recorder::exclusive() || !FlashWriteTest::diagnosticsAllowed() ||
     (GpsReceiver::uartScanBusy()
#if TACTICPOD_REUSABLE
      && strcmp(p,"TCB START")
#endif
     )){
    storeReport("REFUSED: busy/fault",true);return true;}
  if(!storeIdentity()){storeReport(Tcb3Test::Enabled?"REFUSED: identity/status":"REFUSED: JEDEC");return true;}
  if(!strcmp(p,"TCB READ")){
    if(CompactRecorder::capturing()){storeReport("REFUSED: COMP capture active");return true;}
        dump.begin(port);exportSent=0;dumping=true;if(!lineAt)lineSize=0;return true; // A partly sent line is finished first.
  }
  auto c=CompactRecorder::stats();
  if((Tcb3Test::Enabled && testUsed) || (Tcb3Test::Guarded && RamObservations::capturing())){
    storeReport("REFUSED: test used or OBS not stopped");return true;
  }
  if(!storeImuReady || c.capturing || c.inputUsed || c.outputReady){storeReport("REFUSED: IMU/retained compressor RAM");return true;}
#if TACTICPOD_REUSABLE
  if(!BatteryRuntime::monitor().mayStart(MonotonicClock::now())){storeReport("REFUSED: battery untrusted/low");return true;}
#endif
#if TACTICPOD_REUSABLE
  sessionUnlimited=configuredUnlimited || buttonRequest;buttonRequest=false;
  if(!GpsReceiver::prepareRecording()){storeReport("REFUSED: GPS diagnostics busy");return true;}
#if TACTICPOD_BLE
  invalidateBleManifest();
#endif
  waitingGps=true;gpsWaitAt=millis();pending=true;
  storeReport("START waiting for fresh GPS RAM readback; no capture/writes");return true;
#else
  pending=true;store.begin(port,millis());storeReport("START scanning; no auto resume");return true;
#endif
}
bool startFromButton(){
#if TACTICPOD_REUSABLE
#if TACTICPOD_BLE
  if(BleRuntime::active())return false;
#endif
  if(exclusive())return false;
  buttonRequest=true;
  handleCommand("TCB START");
  if(!pending)buttonRequest=false;
  return pending;
#else
  return false;
#endif
}
uint32_t freeSectors(){
  const uint32_t next=store.stats.next;
  return next>=store.floor() && next<=Tcb3Test::WriteEnd?(Tcb3Test::WriteEnd-next)/Tcb3::Sector:0;
}
#if TACTICPOD_BLE && TACTICPOD_REUSABLE
void invalidateBleManifest(){
  useManifest();provisionalMode=false;validating=validationDone=validationFailed=false;
  manifestCache=ManifestCache::Cold;bleListing=false;
  BlePerformance::invalidateCache();
}
void bleIdleService(){
  if(!BleRuntime::active()){
    if(provisionalMode)invalidateBleManifest();
    updateManifestCache(1,false);
  }
}
bool bleEnableProvisional(){
  if(!bleReadAllowed() || bleDownloading || validating)return false;
  bleListing=false;useScanner();provisionalMode=true;
  validationDone=validationFailed=false;
  manifestCache=ManifestCache::Cold;BlePerformance::invalidateCache();
  return true;
}
bool bleBeginList(bool* notReady){
  if(notReady)*notReady=false;
  if(!bleReadAllowed())return false;
  if(bleDownloading || validating){
    bleReport("LIST ownership conflict");return false;
  }
  if(provisionalMode && !scannerActive){
    useScanner();manifestCache=ManifestCache::Cold;bleListing=false;
    validationDone=validationFailed=false;BlePerformance::invalidateCache();
  }
  if(manifestCache!=ManifestCache::Ready){
    if(notReady)*notReady=manifestCache==ManifestCache::Cold || manifestCache==ManifestCache::Scanning;
    bleReport(manifestCache==ManifestCache::Failed?"cache validation failed":"cache not ready; retry LIST");
    return false;
  }
  bleListing=true;bleDownloading=false;
  return true;
}
bool bleBeginValidation(uint32_t id,uint32_t epoch){
  if(!bleReadAllowed() || !provisionalMode || !scannerActive || !bleListDone() ||
     bleDownloading || validating)return false;
  const Tcb3FastDiscovery::Candidate* selected=nullptr;
  for(unsigned i=0;i<bleCache.scanner.count();++i){
    const auto* candidate=bleCache.scanner.entry(i);
    if(candidate && candidate->metadata.sessionId==id && candidate->metadata.resetEpoch==epoch){
      selected=candidate;break;
    }
  }
  if(!selected || !selected->provisional || !selected->metadata.sectorCount ||
     uint64_t(selected->metadata.firstAddress)+uint64_t(selected->metadata.sectorCount)*Tcb3::Sector>Tcb3Test::WriteEnd)
    return false;
  selectedProvisional=selected->metadata;
  const uint32_t end=selectedProvisional.firstAddress+selectedProvisional.sectorCount*Tcb3::Sector;
  bleListing=false;useManifest();
  // Starting at zero makes the existing validator recheck the reset marker and epoch.
  bleCache.manifest.begin(bleReadOnlyPort(),0,end);
  manifestCache=ManifestCache::Scanning;validating=true;validationDone=validationFailed=false;
  return true;
}
bool bleValidationDone(){return validationDone;}
bool bleValidationFailed(){return validationDone && validationFailed;}
bool sameProvisional(const Tcb3::ManifestEntry& a,const Tcb3::ManifestEntry& b){
  if(a.sessionId!=b.sessionId || a.resetEpoch!=b.resetEpoch || a.firstAddress!=b.firstAddress ||
     a.sectorCount!=b.sectorCount || a.formatVersion!=b.formatVersion ||
     a.footerReason!=b.footerReason || a.startUs!=b.startUs || a.stopUs!=b.stopUs ||
     a.deviceId!=b.deviceId || (b.flags&3U)!=3U)return false;
  for(unsigned i=0;i<4;++i)
    if(a.committed[i]!=b.committed[i] || a.lost[i]!=b.lost[i])return false;
  return true;
}
bool bleService(unsigned steps){
  bleFlushDiagnostic();
  if(!BleRuntime::active())return false;
  if(validating){
    if(!steps || !cacheIdle())return false;
    if(!bleWakeFlash()){
      validating=false;validationDone=validationFailed=true;manifestCache=ManifestCache::Failed;
      return false;
    }
    bool progressed=false;
    while(steps-- && !bleCache.manifest.done()){bleCache.manifest.update();progressed=true;}
    if(bleCache.manifest.done()){
      const Tcb3::ManifestEntry* match=nullptr;
      for(unsigned i=0;i<bleCache.manifest.count();++i){
        const auto* entry=bleCache.manifest.entry(i);
        if(entry && sameProvisional(selectedProvisional,*entry)){match=entry;break;}
      }
      validationFailed=bleCache.manifest.failed() || bleCache.manifest.overflow() || !match;
      validating=false;validationDone=true;
      manifestCache=validationFailed?ManifestCache::Failed:ManifestCache::Ready;
      bleListing=!validationFailed;
      if(validationFailed)bleReport(bleCache.manifest.failed()?bleCache.manifest.errorMessage():"provisional session validation mismatch");
    }
    bleSleepFlash();return progressed;
  }
  if(!bleListing && !bleDownloading){updateManifestCache(steps?32:0,true);return false;} // Restore steps for Phase 2.
  bool progressed=false;
  while(steps--){
    if(bleDownloading && !bleDump.done() && !bleDump.failed()){
      unsigned length=0;
      if(bleDump.output(length))break;
      bleDump.update();progressed=true;
      continue;
    }
    break;
  }
  return progressed;
}
bool bleListDone(){return bleListing && manifestCache==ManifestCache::Ready;}
bool bleListFailed(){return manifestCache==ManifestCache::Failed;}
uint16_t bleDiscoveryError(){
  if(!scannerActive || manifestCache!=ManifestCache::Failed)return 4;
  switch(bleCache.scanner.result()){
    case Tcb3FastDiscovery::Result::Overflow:return 8;
    case Tcb3FastDiscovery::Result::Corrupt:return 9;
    case Tcb3FastDiscovery::Result::Ambiguous:return 10;
    case Tcb3FastDiscovery::Result::IoError:return 11;
    case Tcb3FastDiscovery::Result::NotStarted:
    case Tcb3FastDiscovery::Result::Scanning:return 11;
    default:return 4;
  }
}
bool bleListOverflow(){return bleListDone() && (scannerActive?bleCache.scanner.overflow():bleCache.manifest.overflow());}
uint32_t bleIncompleteCount(){return bleListDone()?(scannerActive?bleCache.scanner.incomplete():bleCache.manifest.incompleteCountValue()):0;}
unsigned bleSessionCount(){return bleListDone()?(scannerActive?bleCache.scanner.count():bleCache.manifest.count()):0;}
const Tcb3::ManifestEntry* bleSession(unsigned index){
  const Tcb3::ManifestEntry* entry=nullptr;
  if(bleListDone()){
    if(scannerActive){
      const auto* candidate=bleCache.scanner.entry(index);
      if(candidate && candidate->provisional){
        provisionalEntry=candidate->metadata;provisionalEntry.flags=9U;entry=&provisionalEntry;
      }
    }else entry=bleCache.manifest.entry(index);
  }
  if(!entry)bleReport("manifest entry unavailable");
  return entry;
}
bool bleBeginSession(uint32_t id,uint32_t epoch){
  if(!bleReadAllowed())return false;
  if(!bleListDone() || bleDownloading || scannerActive ||
     (provisionalMode && (!validationDone || validationFailed ||
       selectedProvisional.sessionId!=id || selectedProvisional.resetEpoch!=epoch))){
    bleReport("GET manifest/ownership conflict");return false;
  }
  const Tcb3::ManifestEntry* selected=nullptr;
  for(unsigned i=0;i<bleCache.manifest.count();++i){
    const Tcb3::ManifestEntry* candidate=bleCache.manifest.entry(i);
    if(candidate && candidate->sessionId==id && candidate->resetEpoch==epoch){selected=candidate;break;}
  }
  if(!selected || (selected->flags&3U)!=3U || !selected->sectorCount ||
     uint64_t(selected->firstAddress)+uint64_t(selected->sectorCount)*Tcb3::Sector>Tcb3Test::WriteEnd)
    return false;
  const uint32_t end=selected->firstAddress+selected->sectorCount*Tcb3::Sector;
  if(!bleWakeFlash())return false;
  if(!bleDump.beginSession(port,id,selected->firstAddress,end)){
    bleReport("export initialization failed");bleSleepFlash();return false;
  }
  bleDownloading=true;return true;
}
const uint8_t* blePacket(unsigned& length){return bleDownloading?bleDump.output(length):nullptr;}
void bleReleasePacket(){if(bleDownloading)bleDump.release();}
bool bleTransferDone(){return bleDownloading && bleDump.done();}
bool bleTransferFailed(){return bleDownloading && bleDump.failed();}
void bleFinishList(){bleSleepFlash();}
void bleFinishSession(){bleDownloading=false;bleSleepFlash();}
void bleCancel(){
  bleListing=bleDownloading=false;bleSleepFlash();
  if(validating){
    validating=false;validationDone=validationFailed=false;
    useScanner();manifestCache=ManifestCache::Cold;BlePerformance::invalidateCache();
  }
}
#endif
void update(bool allow){
#if TACTICPOD_REUSABLE
  if(resetting){
    if(allow){
      resetEngine.update(millis());
      if(resetEngine.done()){
        resetting=false;storeReport("RESET complete; recordings hidden, storage restarts at sector 1");
        Recorder::begin(storeImuReady); // Refresh the legacy scan so RECORD READ reflects the reset.
      }else if(resetEngine.failed()){
        resetting=false;resetFailed=true;storeReport("FAULT: reset incomplete; recordings fail closed, repeat RECORD RESET");
      }
    }
    flushLine();
    return;
  }
#endif
#if TACTICPOD_REUSABLE
  BatteryRuntime::noteLoad(owned || pending);
  BatteryRuntime::update(allow && !pageOpen); // Never sample the ADC mid flash-page transaction.
  if(waitingGps && !pending)waitingGps=false;
  if(waitingGps) {
    if(GpsReceiver::recordingReady()) {
      waitingGps=false;store.begin(port,millis());storeReport("START GPS verified; scanning");
    } else if(GpsReceiver::preparationFailed() || uint32_t(millis()-gpsWaitAt)>=90000) {
      waitingGps=false;pending=false;storeReport("REFUSED: GPS profile unverified; no recording");
      FieldTrace::note(FieldTrace::Kind::GpsFailed,millis());
    } else return; // Keep the existing store untouched until receiver readback succeeds.
  }
  if(owned && !locked){
    uint64_t now=MonotonicClock::now();
    const auto& b=BatteryRuntime::monitor();
    if(!finishing && b.trusted(now)){
      if(b.s.stop){batteryEvent(b.s.hard?3:2);stop(Tcb3::Reason::Clean);}
      else if(b.s.warning && !batteryWarningRecorded){batteryEvent(1);batteryWarningRecorded=true;}
    }
    if(!finishing && capReached(now))stop(Tcb3::Reason::Clean);
    if(now>=deadline()){
      stop(Tcb3::Reason::Overflow);store.halt("reusable finalisation deadline");locked=true;pending=false;storeReport("FAULT: drain timeout");
      FieldTrace::note(FieldTrace::Kind::Fault,millis(),store.stats.session);
    }
  }
#endif
  // Limits are enforced independently of USB and foreground-slack permission.
  if(Tcb3Test::Enabled && owned && !locked){
    uint64_t elapsed=MonotonicClock::now()-testStartedUs;
    if(elapsed>=Tcb3Test::CaptureUs && !finishing)stop(Tcb3::Reason::Clean);
    if(elapsed>=Tcb3Test::FinaliseUs){
      stop(Tcb3::Reason::Overflow);store.halt("test finalisation deadline");
      locked=true;pending=false;storeReport("FAULT: test finalisation deadline");
    }
  }
  if(dumping){
    if(!Serial){dumping=false;exportSent=0;return;}
    if(lineAt){flushLine();return;} // Binary packets start only after any partly sent text line has its newline.
    // One bounded read/update and at most two writes: a packet tail can share
    // this pass with the next packet's head, without another packet buffer.
    bool prepared=false;
    for(unsigned writes=0;writes<2;++writes){
      if(!Serial){dumping=false;exportSent=0;return;}
      unsigned n=0;const uint8_t* p=dump.output(n);
      if(!p){
        if(!allow || prepared || dump.done())break;
        dump.update();prepared=true;p=dump.output(n);
        if(!p)break;
      }
      const int room=Serial.availableForWrite();if(room<=0)break;
      unsigned k=unsigned(room);if(k>n-exportSent)k=n-exportSent;
      exportSent+=Serial.write(p+exportSent,k);
      if(exportSent<n)break;
      dump.release();exportSent=0;
    }
    if(dump.done()){dumping=false;exportSent=0;}return;
  }
  if(allow && !locked && (pending || owned)){
    uint64_t before=MonotonicClock::now();store.update(millis());
    uint64_t elapsed=MonotonicClock::now()-before;if(elapsed>maxServiceUs)maxServiceUs=elapsed;
    if(store.state()==Tcb3::State::Fault){
      stop(Tcb3::Reason::Overflow);pending=false;locked=true;storeReport("FAULT: reboot required");
      FieldTrace::note(FieldTrace::Kind::Fault,millis(),store.stats.session);
    }else if(pending && store.state()==Tcb3::State::Idle){
#if TACTICPOD_REUSABLE
      // Conservative raw-fallback admission reserves all headers/footer slots.
      uint32_t blocks=(sessionSeconds+1)/2+1,sectors=sessionUnlimited?UnlimitedMinSectors:(blocks*39+13)/14;
      if(store.stats.next<store.floor() || store.stats.next>=Tcb3Test::WriteEnd ||
         sectors>(Tcb3Test::WriteEnd-store.stats.next)/Tcb3::Sector){
        ++store.stats.full;pending=false;storeReport("REFUSED: insufficient session capacity");return;
      }
      if(!BatteryRuntime::monitor().mayStart(MonotonicClock::now())){
        pending=false;storeReport("REFUSED: battery changed during scan");return;
      }
      RamObservations::handleCommand("OBS STOP");batteryWarningRecorded=false;
#endif
      uint8_t m[Tcb3::MetadataBytes];Tcb3::metadata(m,MonotonicClock::now());
#ifdef NRF_FICR
      DeviceIdentity::Id identity;
      DeviceIdentity::read(identity);
      Tcb3::put(m+16,identity.value(),8);
      Tcb3::put(m+68,1); // Bit 0: device ID present; build ID remains unavailable.
#endif
      if(store.start(m) && CompactRecorder::start()){
        cleanCompletionPending=false;
        testStartedUs=MonotonicClock::now();testUsed=true;
        FieldTrace::note(FieldTrace::Kind::SessionCreated,millis(),store.stats.session);
        memset(offered,0,sizeof(offered));storeMissed=maxServiceUs=0;finishing=false;owned=true;pending=false;storeReport("STARTED");
      }else{pending=false;storeReport("REFUSED: full/retained RAM");}
    }
    if(owned && !locked){
      if(store.released())CompactRecorder::release();
      auto c=CompactRecorder::stats();
      if(c.fault)stop(Tcb3::Reason::Overflow);
      if(store.stats.full)stop(Tcb3::Reason::Full);
      if(!store.stats.full && store.accepting()){
        size_t n;const uint8_t* p=CompactRecorder::output(n);if(p)store.offer(p,n);
      }
      if(finishing && !store.stats.closing && (store.stats.full || (!c.inputUsed && !c.outputReady)))
        store.finish(stopUs,offered,reason,c.rejected,storeMissed);
      if(store.stats.finished){
        // A full partition is finalised with its reserved footer page: a complete, readable session.
        cleanCompletionPending=(reason==Tcb3::Reason::Clean || reason==Tcb3::Reason::Full) && !locked;
        FieldTrace::note(cleanCompletionPending?FieldTrace::Kind::Complete:FieldTrace::Kind::Fault,millis(),store.stats.session,unsigned(reason));
        owned=false;finishing=false;storeReport("STOPPED");
#if TACTICPOD_REUSABLE
        if(reason!=Tcb3::Reason::Clean && reason!=Tcb3::Reason::Full)locked=true;
#endif
        Recorder::begin(storeImuReady); // Refresh legacy append cache; never resume or erase.
      }
    }
  }
#if TACTICPOD_REUSABLE
  if(batActive && !lineSize){
    const unsigned n=batIndex<=8?BatteryRuntime::formatLine(batReport,batIndex,line,sizeof(line))
      :batIndex==9?BatteryRuntime::format(line,sizeof(line)):0;
    if(n){lineSize=n;++batIndex;}else batActive=false;
  }
#endif
  if(!Recorder::exporting())flushLine();
}
}
#endif
