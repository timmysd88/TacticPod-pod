#define TACTICPOD_TCB3_TEST_BUILD 3
#define TCB_SIM_FLASH_BYTES 0x1000000
#define TCB_SIM_USB_CAPACITY 400000
#include <stdint.h>
void appendObservation(const uint8_t*,unsigned);
#define TCB_SIM_OBSERVATION appendObservation
#include "integration.cpp"
uint8_t appendWire[400000],oldFirst[45056];unsigned appendSize=0;
uint32_t appendInfo[20]{};
void appendObservation(const uint8_t* p,unsigned n){
  if(TcbDetail::owned && !TcbDetail::finishing && CompactRecorder::capturing() && appendSize+n<=sizeof(appendWire)){
    memcpy(appendWire+appendSize,p,n);appendSize+=n;
  }
}
extern "C" unsigned appendInit(){
  memset(SPI.memory,255,sizeof(SPI.memory));memset(SPI.finalSector,255,sizeof(SPI.finalSector));
  memset(SPI.memory+0xfff000,0xa5,4096);return 0;
}
extern "C" unsigned appendScenario(unsigned scenario){
  using namespace TcbDetail;
  memcpy(oldFirst,SPI.memory,45056);
  if(scenario==1)SPI.memory[0xb040]=0x5a; // Occupied payload below blank prefix.
  if(scenario==2)SPI.memory[0xff000]=0;
  if(scenario==3)memset(SPI.memory,255,45056);
  if(scenario==11)SPI.memory[0x100000]=0;
  if(scenario==12)SPI.memory[0xb000]=0; // Occupied/corrupt candidate prefix.
  if(scenario==13)SPI.memory[0xbfff]=0x7f; // Partial program at last byte.
  if(scenario==14)SPI.memory[248]^=1; // Ambiguous predecessor CRC.
  if(scenario==15)SPI.memory[0xc040]=0; // Hidden payload in next allocation.
  if(scenario==18)memcpy(SPI.memory+0xb000,SPI.memory,256); // Valid occupied header, wrong append position.
  if(scenario==19){
    SPI.memory[0x1018]^=1; // Individually valid headers with inconsistent metadata.
    Tcb3::put(SPI.memory+0x1000+248,~Tcb3::crc32(SPI.memory+0x1000,248));
  }
  if(scenario==20)SPI.memory[0xa000+248]^=1; // Corrupt second predecessor.
  Serial.connected=false;setup();startupPending=false;waitIdle();
  if(SPI.programs || SPI.erases || SPI.enables || owned)return 1;
  RamObservations::handleCommand("OBS STOP");
  if(scenario==8)advanceUs(0xffffffffU-uint32_t(MonotonicClock::now())-80000000U);
  Tcb3Session::handleCommand("TCB START");
  if(scenario==16){
    Tcb3Session::handleCommand("TCB START"); // Pending duplicate must not reset scan.
    if(!pending || owned || SPI.programs || SPI.enables)return 20;
  }
  for(unsigned n=0;n<200000 && pending && !owned && !locked;++n){
    if(store.state()==Tcb3::State::Probe && (SPI.programs || SPI.enables))return 2;
    cycle();
  }
  if(scenario==1 || scenario==2 || scenario==3 || scenario==11 || scenario==12 || scenario==13 || scenario==14 || scenario==18 || scenario==19 || scenario==20){
    Tcb3Session::handleCommand("TCB START"); // Fault lock refuses another request.
    return locked && !pending && !owned && !SPI.programs && !SPI.enables?0:3;
  }
  if(!owned || locked){appendInfo[15]=SPI.programs;appendInfo[16]=SPI.enables;appendInfo[17]=locked;return 4;}
  uint64_t start=testStartedUs;unsigned expectedStart=store.stats.next;
  if(expectedStart!=0xb000 || store.stats.probeSkipped)return 5;
  if(scenario==16){
    Tcb3Session::handleCommand("TCB START");
    if(!owned || testStartedUs!=start || SPI.programs)return 21;
  }
  if(store.stats.next<0xb000 || store.stats.next>=0x100000)return 6;
  unsigned enablesBefore=SPI.enables;
  if(beginPage(nullptr,0) || beginPage(nullptr,0x1000) || beginPage(nullptr,0xc000) || beginPage(nullptr,0x100000) || beginPage(nullptr,0xfff000) || beginPage(nullptr,0xb001) || SPI.enables!=enablesBefore)return 7;
  if(scenario==6)SPI.memory[store.stats.next+128]=0; // Change after read-only probe.
  if(scenario==17){
    store.stats.session=13; // Independent adapter rejects before WREN.
    if(beginPage(nullptr,0xb000) || SPI.enables!=enablesBefore)return 22;
  }
  mockImuReadCostUs=30;noiseEnabled=1;SPI.programDelayMs=10;
  if(scenario==9)SPI.denyWel=true;
  if(scenario==10)SPI.programDelayMs=250;
  if(scenario==7){
    while(MonotonicClock::now()-start<180001000ULL){advanceUs(1000);Tcb3Session::update(false);}
    return locked && !CompactRecorder::capturing() && !SPI.programs && !memcmp(oldFirst,SPI.memory,45056)?0:18;
  }
  unsigned nextRmc=0,nextGga=0;
  while(MonotonicClock::now()-start<121000000ULL && !locked && !store.stats.finished){
    unsigned us=unsigned(MonotonicClock::now()-start);
    if(us>=nextRmc){rateFrame(false,nextRmc/1000,true);nextRmc+=100000;}
    if(us>=nextGga){rateFrame(true,nextGga/1000,true);nextGga+=1000000;}
    cycle();
    if((scenario==4 || scenario==5) && pageOpen && pageSent>=64 && (scenario==4 || store.stats.blocks>=1)){
      // Host power-interruption model: CS rises with a partial command, then
      // the caller retains only NOR bytes and creates a fresh CPU instance.
      abortPage(nullptr);
      appendInfo[0]=store.stats.next;appendInfo[1]=SPI.highestProgramEnd;appendInfo[2]=store.stats.blocks;
      return memcmp(oldFirst,SPI.memory,45056) || SPI.erases || SPI.violations?8:0;
    }
  }
  for(unsigned n=0;n<300000 && owned && !locked;++n)cycle();
  if(memcmp(oldFirst,SPI.memory,45056) || SPI.erases || SPI.violations || SPI.highestProgramEnd>0x100000)return 9;
  if(scenario==6 || scenario==9 || scenario==17)return locked && !SPI.programs?0:10;
  if(scenario==15)return locked && store.stats.next==0xc000 && SPI.memory[0xc040]==0 && SPI.memory[0xc000]==255?0:23;
  if(locked || owned || !store.stats.finished || storeMissed)return 11;
  if(scenario==10){
    if(!CompactRecorder::stats().rejected || reason!=Tcb3::Reason::Overflow)return 19;
  }else if(CompactRecorder::stats().rejected || stopUs-start<120000000 || stopUs-start>120010000)return 12;
  for(unsigned a=0x100000;a<0xfff000;++a)if(SPI.memory[a]!=255)return 13;
  for(unsigned a=0xfff000;a<0x1000000;++a)if(SPI.memory[a]!=0xa5)return 14;
  if(scenario==1 && SPI.memory[0xb040]!=0x5a)return 15;
  appendInfo[0]=store.stats.next;appendInfo[1]=SPI.highestProgramEnd;appendInfo[2]=SPI.programs;
  appendInfo[3]=appendSize;appendInfo[4]=unsigned(stopUs-start);appendInfo[5]=store.stats.blocks;
  appendInfo[6]=maxServiceUs;appendInfo[7]=store.stats.maxBlockMs;
  appendInfo[8]=CompactRecorder::stats().inputHigh;appendInfo[9]=CompactRecorder::stats().outputHigh;
  appendInfo[10]=store.stats.session;appendInfo[11]=expectedStart;
  appendInfo[13]=uint32_t(start);appendInfo[14]=uint32_t(start>>32);
  waitIdle();Serial.connected=true;Serial.freeSpace=31;captureUsb=true;
  Tcb3Session::handleCommand("TCB READ");
  for(unsigned n=0;n<300000 && Tcb3Session::exporting();++n)cycle();
  appendInfo[12]=usbSize;
  if(Tcb3Session::exporting() || !usbSize || Serial.overfilledWrites)return 16;
  unsigned programsBefore=SPI.programs;
  Tcb3Session::handleCommand("TCB START");
  if(pending || owned || SPI.programs!=programsBefore)return 17;
  return 0;
}
