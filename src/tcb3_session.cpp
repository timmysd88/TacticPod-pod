#include "tcb3_session.h"
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
namespace {
namespace TcbDetail {
Tcb3::Store store;Tcb3::Export dump;
bool storeImuReady=false,pending=false,owned=false,dumping=false,locked=false,finishing=false;
uint64_t offered[4]{};uint64_t stopUs=0;
uint32_t storeMissed=0,maxServiceUs=0;
unsigned exportSent=0;
Tcb3::Reason reason=Tcb3::Reason::Clean;
bool cleanCompletionPending=false;
#if TACTICPOD_REUSABLE
char line[320]{};
#else
char line[256]{};
#endif
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
bool resetting=false,resetArmed=false;uint32_t resetArmedAt=0;
bool mutationBlocked(){return owned || pending || dumping || locked || resetting || Recorder::exclusive();}
#endif
void select(){SPI.beginTransaction(SPISettings(1000000,MSBFIRST,SPI_MODE0));digitalWrite(2,LOW);}
void deselect(){digitalWrite(2,HIGH);SPI.endTransaction();}
void address(uint32_t a){SPI.transfer(a>>16);SPI.transfer(a>>8);SPI.transfer(a);}
int status(void*){if(pageOpen)return -1;select();SPI.transfer(5);int s=SPI.transfer(0);deselect();return s;}
bool read(void*,uint32_t a,uint8_t* p,unsigned n){
  if(pageOpen || !n || n>64 || a>=Tcb3::End || n>Tcb3::End-a)return false;
  select();SPI.transfer(3);address(a);for(unsigned i=0;i<n;++i)p[i]=SPI.transfer(0);deselect();return true;
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
  if(pageOpen || !(owned || pending) || locked || !store.resetEpoch() || (a&4095) || a<Tcb3::Sector || a>=Tcb3Test::WriteEnd)return false;
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
Tcb3::Port port{nullptr,read,beginPage,pageBytes,endPage,abortPage,status,nullptr,eraseStale};
#else
Tcb3::Port port{nullptr,read,beginPage,pageBytes,endPage,abortPage,status};
#endif
bool storeIdentity(){
  for(unsigned i=0;i<5;++i){select();SPI.transfer(0x9f);unsigned a=SPI.transfer(0),b=SPI.transfer(0),c=SPI.transfer(0);deselect();if(a!=11 || b!=64 || c!=24)return false;}
  if(Tcb3Test::Guarded){
    if(status(nullptr)!=0)return false;
    select();SPI.transfer(0x35);uint8_t sr2=SPI.transfer(0);deselect();
    if(sr2!=2)return false; // The measured B-compatible protection/QE state only.
  }
  return true;
}
void storeReport(const char* text){
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
// Read-only identity reads only need the flash idle; a latched fault must not block diagnosis.
bool busy(){return pending || owned || dumping
#if TACTICPOD_REUSABLE
  || resetting
#endif
  ;}
bool recording(){return owned && !finishing;}
bool faulted(){return locked || store.state()==Tcb3::State::Fault || CompactRecorder::stats().fault;}
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
  if(Tcb3Test::Guarded && (!strcmp(p,"RECORD START") || !strcmp(p,"RECORD ERASE") ||
     !strcmp(p,"RECORD ERASE CONFIRM") || !strcmp(p,"ERASE FFF000"))){
    storeReport("REFUSED: test build blocks V1 writes and all erases");return true;
  }
  if(!exclusive())return false;
#if TACTICPOD_REUSABLE
  if(!strcmp(p,"BAT STATUS"))return false;
#endif
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
    resetting=true;resetEngine.begin(resetPort,millis());
    storeReport("RESET started; flash busy until complete");return true;
  }
#endif
  if(strncmp(p,"TCB ",4))return false;
  if(!strcmp(p,"TCB STATUS")){storeReport("STATUS");return true;}
  if(!strcmp(p,"TCB STOP")){stop(Tcb3::Reason::Clean);storeReport("STOP requested");return true;}
  if(strcmp(p,"TCB START") && strcmp(p,"TCB READ")){storeReport("commands: START STOP STATUS READ");return true;}
  if(exclusive() || Recorder::exclusive() || !FlashWriteTest::diagnosticsAllowed() ||
     (GpsReceiver::uartScanBusy()
#if TACTICPOD_REUSABLE
      && strcmp(p,"TCB START")
#endif
     )){
    storeReport("REFUSED: busy/fault");return true;}
  if(!storeIdentity()){storeReport(Tcb3Test::Enabled?"REFUSED: identity/status":"REFUSED: JEDEC");return true;}
  if(!strcmp(p,"TCB READ")){
    if(CompactRecorder::capturing()){storeReport("REFUSED: COMP capture active");return true;}
    dump.begin(port);exportSent=0;dumping=true;lineSize=lineAt=0;return true;
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
  waitingGps=true;gpsWaitAt=millis();pending=true;
  storeReport("START waiting for fresh GPS RAM readback; no capture/writes");return true;
#else
  pending=true;store.begin(port,millis());storeReport("START scanning; no auto resume");return true;
#endif
}
bool startFromButton(){
#if TACTICPOD_REUSABLE
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
void update(bool allow){
#if TACTICPOD_REUSABLE
  if(resetting){
    if(allow){
      resetEngine.update(millis());
      if(resetEngine.done()){
        resetting=false;storeReport("RESET complete; recordings hidden, storage restarts at sector 1");
        Recorder::begin(storeImuReady); // Refresh the legacy scan so RECORD READ reflects the reset.
      }else if(resetEngine.failed()){
        resetting=false;storeReport("FAULT: reset incomplete; recordings fail closed, repeat RECORD RESET");
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
    // Packet is immutable; USB writes may split it without blocking.
    unsigned n=0;const uint8_t* p=dump.output(n);
    if(p){int room=Serial.availableForWrite();unsigned k=room>0?unsigned(room):0;if(k>n-exportSent)k=n-exportSent;
      if(k)exportSent+=Serial.write(p+exportSent,k);
      if(exportSent==n){dump.release();exportSent=0;}}
    else if(allow)dump.update();
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
      Tcb3::put(m+16,uint64_t(NRF_FICR->DEVICEID[0])|(uint64_t(NRF_FICR->DEVICEID[1])<<32),8);
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
