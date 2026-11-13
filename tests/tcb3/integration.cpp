#define TACTICPOD_TCB3_FLASH_ENABLED 1
#include <stdint.h>
#include <stddef.h>
void spiTime();
short noisyAxis(unsigned);
void usbSink(const uint8_t*,size_t);
#ifdef TCB_LONG_RUN
void compressionTime();
#define TCB_SIM_COMP_UNIT compressionTime
#ifndef TCB_SIM_FLASH_BYTES
#define TCB_SIM_FLASH_BYTES 0xfff000
#endif
#endif
#define TCB_SIM_SPI_TRANSFER spiTime
#define TCB_SIM_IMU_AXIS noisyAxis
#define TCB_SIM_USB_WRITE usbSink
#include "../gps/harness.cpp"
#include "../../src/tcb3_store.cpp"
#include "../../src/tcb3_session.cpp"
uint64_t captureWireUs=0;
unsigned noiseEnabled=0,noiseState=1234567;
#ifndef TCB_SIM_USB_CAPACITY
#define TCB_SIM_USB_CAPACITY 100000
#endif
uint8_t usbBytes[TCB_SIM_USB_CAPACITY];unsigned usbSize=0;
uint8_t preservedPrefix[65536];
bool captureUsb=false;
void usbSink(const uint8_t* p,size_t n){if(captureUsb && usbSize+n<=sizeof(usbBytes)){memcpy(usbBytes+usbSize,p,n);usbSize+=n;}}
short noisyAxis(unsigned axis){
  static const int16_t constant[]={100,200,16384,10,20,30};
  if(!noiseEnabled)return constant[axis];noiseState=noiseState*1664525+1013904223;return int16_t(noiseState>>16);
}
void advanceUs(unsigned n){mockSubUs+=n;mockTime+=mockSubUs/1000;mockSubUs%=1000;}
void compressionTime(){advanceUs(100);} // Declared stress budget per active unit, not measured WCET.
void spiTime(){advanceUs(8);captureWireUs+=8;} // Exact 1MHz byte wire time.
uint32_t integrationInfo[24]{};
uint8_t expectedWire[400000];unsigned expectedSize=0;
uint64_t lastImu=0,minImu=UINT64_MAX,maxImu=0;
unsigned observedImu=0,observedRmc=0,observedGga=0;
bool recordingExpected=false;
bool drainDiagnosticQueues=true;
void drainObserved(){
  RamObservations::Imu i;RamObservations::Rmc r;RamObservations::Gga g;RamObservations::Event e;
  // Collect the canonical stream through observer queues, then sort by the
  // shared timestamp on the host (equal timestamp RMC/GGA precede IMU here).
  while(RamObservations::pop(i)){
    if(!recordingExpected)continue;
    uint64_t t=ObsTests::value(i.bytes+4,8);if(observedImu){if(t-lastImu<minImu)minImu=t-lastImu;if(t-lastImu>maxImu)maxImu=t-lastImu;}
    lastImu=t;++observedImu;if(expectedSize+24<=sizeof(expectedWire)){memcpy(expectedWire+expectedSize,i.bytes,24);expectedSize+=24;}
  }
  while(RamObservations::pop(r)){if(recordingExpected){++observedRmc;if(expectedSize+44<=sizeof(expectedWire)){memcpy(expectedWire+expectedSize,r.bytes,44);expectedSize+=44;}}}
  while(RamObservations::pop(g)){if(recordingExpected){++observedGga;if(expectedSize+40<=sizeof(expectedWire)){memcpy(expectedWire+expectedSize,g.bytes,40);expectedSize+=40;}}}
  while(RamObservations::pop(e)){}
}
#ifdef TCB_SIM_CYCLE_HOOK
void TCB_SIM_CYCLE_HOOK();
#endif
void cycle(){
#ifdef TCB_SIM_CYCLE_HOOK
  TCB_SIM_CYCLE_HOOK();
#endif
  loop();if(drainDiagnosticQueues)drainObserved();advanceUs(100);}
void waitIdle(){for(unsigned n=0;n<200000 && Recorder::exclusive();++n)cycle();}
extern "C" unsigned integration(unsigned scenario){
  using namespace TcbDetail;
  memset(SPI.memory,255,sizeof(SPI.memory));memset(SPI.finalSector,255,sizeof(SPI.finalSector));
  Serial.connected=false;setup();startupPending=false;waitIdle();
  mockImuReadCostUs=30; // Six raw reads: 180us model, plus measured SPI byte cost.
  RamObservations::handleCommand("OBS START");
  Tcb3Session::handleCommand("TCB START");
  for(unsigned n=0;n<200000 && !owned && !locked;++n)cycle();
  if(!owned || locked)return 1;
  recordingExpected=true;
  if(scenario==6)drainDiagnosticQueues=false;
  expectedSize=observedImu=observedRmc=observedGga=0;minImu=UINT64_MAX;maxImu=lastImu=0;
  if(scenario==1)SPI.programDelayMs=250; // Deliberately insufficient sustained bandwidth.
  if(scenario==1 || scenario==4 || scenario>=7)noiseEnabled=1;
  if(scenario>=7)SPI.programDelayMs=10;
  uint32_t start=mockTime,nextRmc=0,nextGga=0;
  unsigned duration=scenario>=7?120000:15000;
  while(uint32_t(mockTime-start)<duration && owned && !finishing && !locked){
    uint32_t elapsed=mockTime-start;
    if(elapsed>=nextRmc){rateFrame(false,nextRmc,true);nextRmc+=100;}
    if(elapsed>=nextGga){rateFrame(true,nextGga,true);nextGga+=1000;}
    cycle();
  }
  if(scenario==2){
    Tcb3Session::noteMissed(1);
  }
  Tcb3Session::handleCommand("TCB STOP");
  recordingExpected=false;
  for(unsigned n=0;n<1000000 && owned && !locked;++n)cycle();
  RamObservations::handleCommand("OBS STOP");
  integrationInfo[0]=observedImu;integrationInfo[1]=observedRmc;integrationInfo[2]=observedGga;
  integrationInfo[3]=uint32_t(minImu);integrationInfo[4]=uint32_t(maxImu);
  integrationInfo[5]=CompactRecorder::stats().rejected;integrationInfo[6]=CompactRecorder::stats().inputHigh;
  integrationInfo[7]=CompactRecorder::stats().outputHigh;integrationInfo[8]=storeMissed;
  integrationInfo[9]=store.stats.blocks;integrationInfo[10]=store.stats.pages;
  integrationInfo[11]=maxServiceUs;integrationInfo[12]=store.stats.next;
  integrationInfo[13]=unsigned(store.state());integrationInfo[14]=store.stats.finished;
  integrationInfo[15]=expectedSize;
  integrationInfo[17]=RamObservations::stats(1).dropped;
  integrationInfo[18]=RamObservations::stats(2).dropped;
  integrationInfo[19]=RamObservations::stats(3).dropped;
  integrationInfo[20]=store.stats.maxBlockMs;
  integrationInfo[21]=store.stats.programOperations;
  if(SPI.erases || SPI.violations || locked || owned || !store.stats.finished)return 2;
  if((scenario==0 || scenario==4) && (observedImu<1498 || observedImu>1502 || observedRmc!=150 || observedGga!=15 ||
      CompactRecorder::stats().rejected || storeMissed || maxImu>=20000))return 3;
  if(scenario==1 && !CompactRecorder::stats().rejected)return 4;
  if(scenario==2 && storeMissed!=1)return 5;
  if(scenario==6 && (!integrationInfo[17] || !integrationInfo[18] || !integrationInfo[19] ||
      store.stats.committed[0]!=1500 || store.stats.committed[1]!=150 || store.stats.committed[2]!=15 ||
      CompactRecorder::stats().rejected || storeMissed))return 19;
  if(scenario>=7 && (observedImu<11998 || observedImu>12002 || observedRmc!=1200 || observedGga!=120 ||
     CompactRecorder::stats().rejected || storeMissed || maxImu>=20000))return 20;
  waitIdle(); // Legacy append pointer must include the compact session.
  if(nextSector!=store.stats.next)return 6;
  if(scenario==5){
    unsigned oldEnd=store.stats.next;memcpy(preservedPrefix,SPI.memory,oldEnd);
    SerialConsole::handleCommand("RECORD START");
    if(!Recorder::recording())return 12;
    if(!Tcb3Session::handleCommand("TCB START") || owned || pending)return 13;
    uint32_t end=mockTime+1000;while(int32_t(mockTime-end)<0)cycle();
    SerialConsole::handleCommand("RECORD STOP");waitIdle();
    if(memcmp(preservedPrefix,SPI.memory,oldEnd))return 14;
    if(Tcb3::get(SPI.memory+oldEnd)!=0x31485054)return 15;
    uint32_t legacyEnd=nextSector;
    Tcb3Session::handleCommand("TCB START");
    for(unsigned n=0;n<200000 && !owned && !locked;++n)cycle();
    if(!owned || store.stats.session!=legacyEnd/4096+1)return 16;
    end=mockTime+1000;while(int32_t(mockTime-end)<0)cycle();
    Tcb3Session::handleCommand("TCB STOP");
    for(unsigned n=0;n<200000 && owned && !locked;++n)cycle();
    if(owned || locked || memcmp(preservedPrefix,SPI.memory,oldEnd))return 17;
  }
  // Ownership tests while exporting: arbitrary commands cannot corrupt binary.
  if(scenario==3){
    Tcb3Session::handleCommand("TCB READ");
    if(!Tcb3Session::guardCommand("RECORD START") || !Recorder::exporting())return 7;
    // The pump may now write twice per update, so model USB space being consumed: 31 bytes stay the cap.
    Serial.connected=true;Serial.freeSpace=31;Serial.consumeWriteSpace=true;
    Tcb3Session::update();Tcb3Session::update();
    Serial.consumeWriteSpace=false;
    if(TcbDetail::exportSent!=31 || Serial.overfilledWrites)return 18;
    Serial.connected=false;Tcb3Session::update();if(Tcb3Session::exporting())return 8;
    Tcb3Session::handleCommand("TCB READ");Serial.connected=true;Serial.freeSpace=0;captureUsb=true;
    unsigned writesBefore=SPI.programs;
    for(unsigned i=0;i<100;++i)cycle();if(usbSize)return 9;
    if(!SerialConsole::handleCommand("RECORD START"))return 10;
    Serial.freeSpace=31;
    for(unsigned n=0;n<100000 && Tcb3Session::exporting();++n)cycle();
    if(Tcb3Session::exporting() || SPI.programs!=writesBefore || Serial.overfilledWrites)return 11;
    integrationInfo[16]=usbSize;
  }
  return 0;
}
