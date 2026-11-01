#define TACTICPOD_REUSABLE 1
#define TACTICPOD_BATTERY_SIM 1
#define TACTICPOD_BATTERY_STORE_SIM 1
#define TACTICPOD_TCB3_FLASH_ENABLED 1
#define TCB_SIM_FLASH_BYTES 0x1000000
#define TCB_SIM_USB_CAPACITY 700000
#include <stdint.h>
void reusableObservation(const uint8_t*,unsigned);
#define TCB_SIM_OBSERVATION reusableObservation
#include "../tcb3/integration.cpp"
#include "../../src/battery_calibration_store.cpp"
#include "../../src/battery_runtime.cpp"
uint8_t protectedPrefix[0x39000],newWire[200000];unsigned newWireSize=0;
uint32_t reusableInfo[24]{};
void reusableObservation(const uint8_t* p,unsigned n){
  if(TcbDetail::owned && !TcbDetail::finishing && CompactRecorder::capturing()){
    if(newWireSize+n>sizeof(newWire))return;
    memcpy(newWire+newWireSize,p,n);newWireSize+=n;
  }
}
extern "C" unsigned reusableInit(){
  memset(SPI.memory,255,sizeof(SPI.memory));memset(SPI.finalSector,255,sizeof(SPI.finalSector));
  memset(SPI.memory+0xfff000,0xa5,4096);return 0;
}
extern "C" unsigned reusableCase(unsigned c){
  using namespace TcbDetail;
  memcpy(protectedPrefix,SPI.memory,sizeof(protectedPrefix));
  if(c==4)SPI.memory[0x39040]=0;
  if(c==5)SPI.memory[0xffd000]=0; // At most one free final recording sector.
  Serial.connected=false;setup();startupPending=false;waitIdle();
  if(owned || pending || SPI.programs || SPI.enables || RamObservations::capturing())return 1;
  if(!Tcb3Session::guardCommand("RECORD START") || !Tcb3Session::guardCommand("RECORD ERASE") ||
     !Tcb3Session::guardCommand("RECORD ERASE CONFIRM") || !Tcb3Session::guardCommand("ERASE FFF000"))return 2;
  Recorder::handleCommand("RECORD START");Recorder::handleCommand("RECORD ERASE");Recorder::handleCommand("RECORD ERASE CONFIRM");
  if(SPI.programs || SPI.erases || SPI.enables)return 3;
  BatteryRuntime::inject(4000,true);
  if(c==1 || c==2){
    BatteryRuntime::monitor().calibrate(1000000);BatteryRuntime::monitor().cell(true);
    for(unsigned n=0;n<40000;++n)cycle();
    if(!BatteryRuntime::monitor().trusted(MonotonicClock::now()))return 4;
  }
  if(c==3)BatteryRuntime::monitor().cell(true); // Not calibrated: refusal, not shutdown.
  unsigned sessions=c==0?3:1;
  if(c==11){Serial.connected=true;SerialConsole::printMenu();if(!strstr(Serial.output,"BAT STATUS"))return 21;}
  mockImuReadCostUs=30;noiseEnabled=1;SPI.programDelayMs=10;
  if(c==8)advanceUs(0xffffffffU-uint32_t(MonotonicClock::now())-4000000U);
  for(unsigned run=0;run<sessions;++run){
    waitIdle();Tcb3Session::handleCommand((c==1 || c==2)?"TCB DURATION 30":c==8?"TCB DURATION 8":"TCB DURATION 4");
    if(sessionSeconds!=((c==1 || c==2)?30U:c==8?8U:4U))return 5;
    Tcb3Session::handleCommand("TCB START");
    for(unsigned n=0;n<200000 && pending && !owned && !locked;++n)cycle();
    if(c==3)return !owned && !pending && !SPI.programs && !BatteryRuntime::monitor().s.stop?0:6;
    if(c==4)return locked && !SPI.programs && !SPI.enables?0:7;
    if(c==5)return !owned && !pending && !SPI.programs && !SPI.enables?0:8;
    if(!owned || locked || store.stats.session<58 || store.initialAddress()<0x39000)return 9;
    const uint64_t start=testStartedUs;
    Tcb3Session::handleCommand("TCB START");if(testStartedUs!=start || !owned)return 10;
    Tcb3Session::handleCommand("BAT MODE CELL");
    if(c==0 && BatteryRuntime::monitor().s.cell)return 11; // Config cannot change during capture.
    if(c==7){
      advanceUs(5000000);Tcb3Session::update(false);advanceUs(60001000);Tcb3Session::update(false);
      return locked && !CompactRecorder::capturing() && !SPI.programs?0:12;
    }
    unsigned rmcAt=0,ggaAt=0,batAt=0;
    while(owned && !locked && MonotonicClock::now()-start<40000000){
      unsigned elapsed=unsigned(MonotonicClock::now()-start);
      if(c==11 && elapsed>=batAt){SerialConsole::handleCommand("BAT STATUS");batAt+=1000000;}
      if(c==1 && elapsed>=5000000)BatteryRuntime::inject(3400,true);
      if(c==2 && elapsed>=5000000)BatteryRuntime::inject(3200,true);
      if(c==9)BatteryRuntime::inject(2500,true); // USB-only phantom low never stops.
      if(elapsed>=rmcAt){rateFrame(false,rmcAt/1000,true);rmcAt+=100000;}
      if(elapsed>=ggaAt){rateFrame(true,ggaAt/1000,true);ggaAt+=1000000;}
      cycle();
      if((c==6 || c==12) && pageOpen && pageSent>=64 && (c==6 || store.stats.blocks>=1)){
        abortPage(nullptr);reusableInfo[0]=store.stats.next;return 0;
      }
      if(c==10 && elapsed>=1000000 && !finishing)Tcb3Session::noteMissed(1);
    }
    if(!store.stats.finished || owned)return 13;
    if(c==10)return locked && storeMissed==1 && reason==Tcb3::Reason::Deadline?0:14;
    if(locked || CompactRecorder::stats().rejected || storeMissed)return 15;
    if((c==1 || c==2) && (stopUs-start>=30000000 || !store.stats.committed[3]))return 16;
    if(c==0 || c==8 || c==9){
      if(stopUs-start<captureLimit() || stopUs-start>captureLimit()+10000)return 17;
    }
    waitIdle();
  }
  if(c==1 || c==2){
    unsigned before=SPI.programs;
    Tcb3Session::handleCommand("TCB START");if(owned || pending || SPI.programs!=before)return 22;
    BatteryRuntime::inject(4000,true);
    for(unsigned n=0;n<30;++n){advanceUs(1000000);Tcb3Session::update(false);}
    if(!BatteryRuntime::monitor().mayStart(MonotonicClock::now()) || owned || pending || SPI.programs!=before)return 23;
  }
  if(memcmp(protectedPrefix,SPI.memory,sizeof(protectedPrefix)) || SPI.erases || SPI.violations)return 18;
  for(unsigned a=0xfff000;a<0x1000000;++a)if(SPI.memory[a]!=0xa5)return 19;
  reusableInfo[0]=store.stats.next;reusableInfo[1]=SPI.programs;reusableInfo[2]=newWireSize;
  reusableInfo[3]=maxServiceUs;reusableInfo[4]=store.stats.maxBlockMs;reusableInfo[5]=store.stats.session;
  reusableInfo[7]=unsigned(stopUs-testStartedUs);reusableInfo[8]=CompactRecorder::stats().inputHigh;
  reusableInfo[9]=CompactRecorder::stats().outputHigh;
  Serial.connected=true;Serial.freeSpace=31;captureUsb=true;Tcb3Session::handleCommand("TCB READ");
  for(unsigned n=0;n<300000 && Tcb3Session::exporting();++n){
    if(c==11 && n%100==0)SerialConsole::handleCommand("BAT STATUS"); // Must inject no text into TPX3.
    cycle();
  }
  reusableInfo[6]=usbSize;
  return Tcb3Session::exporting() || !usbSize || Serial.overfilledWrites?20:0;
}
