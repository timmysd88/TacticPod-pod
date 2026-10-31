#define TACTICPOD_TCB3_TEST_BUILD 1
#define TCB_SIM_FLASH_BYTES 0x1000000
#include <stdint.h>
void firstObservation(const uint8_t*,unsigned);
#define TCB_SIM_OBSERVATION firstObservation
#include "integration.cpp"
uint8_t firstWire[40000];unsigned firstSize=0;
uint32_t firstInfo[16]{};
void firstObservation(const uint8_t* p,unsigned n){
  if(TcbDetail::owned && !TcbDetail::finishing && CompactRecorder::capturing() && firstSize+n<=sizeof(firstWire)){
    memcpy(firstWire+firstSize,p,n);firstSize+=n;
  }
}
extern "C" unsigned firstWrite(unsigned scenario){
  using namespace TcbDetail;
  memset(SPI.memory,255,sizeof(SPI.memory));memset(SPI.finalSector,255,sizeof(SPI.finalSector));
  memset(SPI.memory+0xfff000,0xa5,4096);
  Serial.connected=false;setup();startupPending=false;waitIdle();
  if(SPI.programs || SPI.erases)return 1;
  RamObservations::handleCommand("OBS STOP");
  // Direct public V1 entry points cannot bypass the test console guard.
  Recorder::handleCommand("RECORD START");Recorder::handleCommand("RECORD ERASE");Recorder::handleCommand("RECORD ERASE CONFIRM");
  if(Recorder::exclusive() || SPI.programs || SPI.erases)return 2;
  if(!Tcb3Session::guardCommand("ERASE FFF000"))return 3;
  if(scenario==1)SPI.testSr2=0x82;
  if(scenario==2)SPI.memory[0x10000-4096]=0; // Occupied sector at edge of fence.
  if(scenario==3)SPI.memory[0x10000]=0; // Occupied sector beyond fence, still scanned.
  if(scenario==4)RamObservations::handleCommand("OBS START");
  if(scenario==8)advanceUs(0xffffffffU-uint32_t(MonotonicClock::now())-5000000U);
  Tcb3Session::handleCommand("TCB START");
  for(unsigned n=0;n<100000 && pending && !owned && !locked;++n)cycle();
  if(scenario>=1 && scenario<=4)return !owned && !pending && !SPI.programs && !SPI.erases?0:4;
  if(!owned || locked)return 5;
  unsigned enablesBefore=SPI.enables;
  if(beginPage(nullptr,0x10000) || beginPage(nullptr,0xfff000) || beginPage(nullptr,1) || SPI.enables!=enablesBefore)return 18;
  const uint64_t start=testStartedUs;
  mockImuReadCostUs=30;noiseEnabled=1;SPI.programDelayMs=10;
  if(scenario==5){
    // Starve persistence, including with allowWork=false. Deadlines still fire.
    while(MonotonicClock::now()-start<16000000ULL){advanceUs(1000);Tcb3Session::update(false);}
    if(!locked || CompactRecorder::capturing() || !store.error)return 6;
  }else if(scenario==7){
    for(unsigned n=0;n<10000 && !(pageOpen && pageSent>=64);++n)cycle();
    if(!pageOpen)return 15;
    advanceUs(unsigned(Tcb3Test::FinaliseUs-(MonotonicClock::now()-start)));
    Tcb3Session::update(false);
    if(!locked || pageOpen || SPI.transactionActive || !store.stats.incompleteWrites)return 16;
    unsigned programsBefore=SPI.programs;
    for(unsigned n=0;n<1000;++n)cycle();
    if(SPI.programs!=programsBefore)return 17;
  }else if(scenario==6){
    Tcb3Session::noteMissed(1);
  }else{
    unsigned nextRmc=0,nextGga=0;
    while(MonotonicClock::now()-start<6000000ULL && !locked){
      unsigned us=unsigned(MonotonicClock::now()-start);
      if(us>=nextRmc){rateFrame(false,nextRmc/1000,true);nextRmc+=100000;}
      if(us>=nextGga){rateFrame(true,nextGga/1000,true);nextGga+=1000000;}
      cycle(); // USB remains disconnected; no manual STOP is sent.
    }
  }
  for(unsigned n=0;n<200000 && owned && !locked;++n)cycle();
  firstInfo[0]=store.stats.next;firstInfo[1]=SPI.highestProgramEnd;firstInfo[2]=SPI.programs;
  firstInfo[3]=locked;firstInfo[4]=store.stats.finished;firstInfo[5]=storeMissed;
  firstInfo[6]=firstSize;firstInfo[7]=unsigned(stopUs-start);firstInfo[8]=unsigned(reason);
  firstInfo[9]=CompactRecorder::stats().outputHigh;firstInfo[10]=maxServiceUs;
  firstInfo[12]=uint32_t(start);firstInfo[13]=uint32_t(start>>32);
  if(SPI.erases || SPI.violations || SPI.highestProgramEnd>0x10000)return 7;
  for(unsigned a=0x10000;a<0xfff000;++a)if(SPI.memory[a]!=255)return 8;
  for(unsigned a=0xfff000;a<0x1000000;++a)if(SPI.memory[a]!=0xa5)return 8;
  if(scenario==5 || scenario==7)return locked && !CompactRecorder::capturing()?0:9;
  if(owned || locked || !store.stats.finished)return 10;
  if((scenario==0 || scenario==8) && (stopUs-start<5000000 || stopUs-start>5010000 || storeMissed || CompactRecorder::stats().rejected))return 11;
  unsigned old=SPI.programs;
  Tcb3Session::handleCommand("TCB START");
  if(pending || owned || SPI.programs!=old)return 12;
  // SPI adapter independently rejects bad destinations before WREN.
  if(beginPage(nullptr,0x10000) || beginPage(nullptr,0xfff000) || beginPage(nullptr,1) || SPI.programs!=old)return 13;
  if(scenario==0 || scenario==8){
    waitIdle();Serial.connected=true;Serial.freeSpace=31;captureUsb=true;
    Tcb3Session::handleCommand("TCB READ");
    for(unsigned n=0;n<100000 && Tcb3Session::exporting();++n)cycle();
    firstInfo[11]=usbSize;
    if(Tcb3Session::exporting() || SPI.programs!=old || !usbSize)return 14;
  }
  return 0;
}
