// Actual acquisition/session/compressor/store code, simulated NOR and USB only.
#include "integration.cpp"
uint32_t switchlessInfo[16]{};
extern "C" unsigned switchlessCase(unsigned scenario){
  using namespace TcbDetail;
  memcpy(protectedPrefix,SPI.memory,sizeof(protectedPrefix));
  Serial.connected=false;setup();startupPending=false;waitIdle();
  for(unsigned n=0;n<1000;++n)cycle();
  if(owned || pending || SPI.programs || RamObservations::capturing())return 1;
  BatteryRuntime::inject(3900,true); // Model data, NOT a real voltage/calibration.
  Serial.connected=true;cycle();SerialConsole::printMenu();
  SerialConsole::handleCommand("TCB DURATION 1");if(sessionSeconds!=1)return 2;
  SerialConsole::handleCommand("TCB DURATION 1800");if(sessionSeconds!=1800)return 3;
  SerialConsole::handleCommand("TCB DURATION 0");if(sessionSeconds!=1800)return 4;
  SerialConsole::handleCommand("TCB DURATION 1801");if(sessionSeconds!=1800)return 5;
  SerialConsole::handleCommand("TCB DURATION 10");if(sessionSeconds!=10)return 6;
  SerialConsole::handleCommand("BAT MODE USB");
  if(BatteryRuntime::monitor().s.cell || BatteryRuntime::monitor().s.calibrated)return 7;
  mockImuReadCostUs=30;noiseEnabled=1;SPI.programDelayMs=10;
  SerialConsole::handleCommand("TCB START");
  for(unsigned n=0;n<200000 && pending && !owned && !locked;++n)cycle();
  if(!owned || locked || store.stats.session!=58 || store.initialAddress()!=0x39000)return 8;
  uint64_t start=testStartedUs;unsigned rmcAt=0,ggaAt=0;
  while(owned && !locked && MonotonicClock::now()-start<70000000){
    unsigned elapsed=unsigned(MonotonicClock::now()-start);
    if(elapsed>=2000000)Serial.connected=false;
    if(scenario && elapsed>=3000000 && elapsed<5000000)Serial.connected=true;
    if(scenario && elapsed>=6000000 && elapsed<7000000)Serial.connected=true;
    if(!finishing){
      if(elapsed>=rmcAt){rateFrame(false,rmcAt/1000,true);rmcAt+=100000;}
      if(elapsed>=ggaAt){rateFrame(true,ggaAt/1000,true);ggaAt+=1000000;}
      if(elapsed<9900000 && !CompactRecorder::capturing())return 9;
    }
    cycle();
  }
  if(locked || owned || !store.stats.finished || reason!=Tcb3::Reason::Clean || storeMissed ||
     CompactRecorder::stats().rejected || store.stats.crcFailures || store.stats.incompleteWrites)return 10;
  if(stopUs-start<10000000 || stopUs-start>10010000)return 11;
  waitIdle();const unsigned programs=SPI.programs;
  Serial.connected=true;
  for(unsigned n=0;n<10000;++n)cycle();
  SerialConsole::handleCommand("TCB STATUS");SerialConsole::handleCommand("BAT STATUS");
  if(owned || pending || SPI.programs!=programs)return 12;
  switchlessInfo[0]=store.stats.next;switchlessInfo[1]=newWireSize;
  switchlessInfo[2]=unsigned(stopUs-start);switchlessInfo[3]=programs;
  switchlessInfo[4]=store.stats.session;switchlessInfo[5]=maxServiceUs;
  switchlessInfo[6]=store.stats.maxBlockMs;
  Serial.freeSpace=31;captureUsb=true;
  usbSize=0;SerialConsole::handleCommand("TCB READ");
  for(unsigned n=0;n<300000 && Tcb3Session::exporting();++n)cycle();
  if(Tcb3Session::exporting() || !usbSize || Serial.overfilledWrites)return 13;
  switchlessInfo[7]=usbSize; // Host compares another export via helper below.
  switchlessInfo[8]=SPI.pageReads;
  if(!SPI.pageReads)return 16;
  if(memcmp(protectedPrefix,SPI.memory,sizeof(protectedPrefix)) || SPI.erases || SPI.violations)return 14;
  for(unsigned a=0xfff000;a<0x1000000;++a)if(SPI.memory[a]!=0xa5)return 15;
  return 0;
}
extern "C" unsigned switchlessSecondExport(){
  unsigned before=SPI.programs;usbSize=0;
  SerialConsole::handleCommand("TCB READ");
  for(unsigned n=0;n<300000 && Tcb3Session::exporting();++n)cycle();
  switchlessInfo[7]=usbSize;
  return !usbSize || Tcb3Session::exporting() || SPI.programs!=before;
}
