// RECORD RESET through the real console, session, compressor and store code, with paced 100 Hz IMU and 10 Hz GPS.
// Host simulation only: it shows logic and queue behaviour under MODELLED erase times, not real NOR timing.
#include "integration.cpp"
using namespace TcbDetail;

uint32_t resetInfo[40]{};
uint64_t maxCycleUs=0;
static bool has(const char* s){return strstr(Serial.output,s)!=nullptr;}
static void clearOutput(){Serial.used=0;Serial.output[0]=0;usbSize=0;}
static void run(unsigned n){for(unsigned i=0;i<n;++i)cycle();}
static void bootDevice(){
  Serial.connected=false;setup();startupPending=false;waitIdle();Serial.connected=true;
  Serial.freeSpace=256;mockImuReadCostUs=30;SPI.programDelayMs=10;BatteryRuntime::inject(4000,true);
  clearOutput();captureUsb=false;maxCycleUs=0;
}
static void duration(unsigned seconds){
  char cmd[32]="TCB DURATION ";unsigned k=13;char d[12];unsigned n=0;
  do{d[n++]=char('0'+seconds%10);seconds/=10;}while(seconds);
  while(n)cmd[k++]=d[--n];cmd[k]=0;SerialConsole::handleCommand(cmd);
}
static bool startRecording(){
  SerialConsole::handleCommand("TCB START");
  for(unsigned i=0;i<400000 && pending && !owned && !locked;++i)cycle();
  return owned && !locked;
}
// Paced producer: RMC every 100 ms, GGA every second, one main-loop pass per 100 us plus modelled SPI/compressor time.
static bool feed(unsigned seconds,uint64_t missAtUs=0){
  const uint64_t start=testStartedUs;unsigned rmcAt=0,ggaAt=0;bool missed=false;
  while(owned && !locked && MonotonicClock::now()-start<uint64_t(seconds+40)*1000000){
    const unsigned elapsed=unsigned(MonotonicClock::now()-start);
    if(elapsed>=rmcAt){rateFrame(false,rmcAt/1000,true);rmcAt+=100000;}
    if(elapsed>=ggaAt){rateFrame(true,ggaAt/1000,true);ggaAt+=1000000;}
    if(missAtUs && !missed && elapsed>=missAtUs){Tcb3Session::noteMissed(1);missed=true;}
    const uint64_t t0=MonotonicClock::now();cycle();
    const uint64_t dt=MonotonicClock::now()-t0;if(dt>maxCycleUs)maxCycleUs=dt;
  }
  return store.stats.finished && !owned;
}
// Keeps the GPS stream alive (as the real receiver does) until the UART scan and V1 scan are quiet.
static bool settle(){
  const uint64_t start=MonotonicClock::now();unsigned rmcAt=0;
  for(unsigned n=0;n<600000;++n){
    const unsigned elapsed=unsigned(MonotonicClock::now()-start);
    if(elapsed>3000000 && !Recorder::exclusive() && !GpsReceiver::uartScanBusy())return true;
    if(elapsed>=rmcAt){rateFrame(false,rmcAt/1000,true);rmcAt+=100000;}
    cycle();
  }
  return false;
}
static void liveRun(unsigned passes){
  static unsigned phase=0;
  for(unsigned n=0;n<passes;++n){if(++phase%1000==0)rateFrame(false,phase/1000,true);cycle();}
}
static bool exportAll(unsigned limit=600000,bool noise=false){
  clearOutput();Serial.connected=true;captureUsb=true;SerialConsole::handleCommand("TCB READ");
  if(!Tcb3Session::exporting())return false;
  unsigned lcg=12345;
  for(unsigned n=0;n<limit && Tcb3Session::exporting();++n){
    if(noise){
      lcg=lcg*1664525+1013904223;Serial.freeSpace=int((lcg>>8)%257);
      if(n%97==0)SerialConsole::handleCommand("TCB STATUS");
      if(n%97==31)SerialConsole::handleCommand("BAT STATUS");
      if(n%97==61)SerialConsole::handleCommand("DIAG ID");
      if(n%97==83)SerialConsole::handleCommand("RECORD READ");
    }
    cycle();
  }
  const bool done=!Tcb3Session::exporting();Serial.freeSpace=256;captureUsb=false;return done;
}
static void recordMetrics(){
  const auto cs=CompactRecorder::stats();
  resetInfo[0]=locked;resetInfo[1]=store.stats.finished;resetInfo[2]=unsigned(cs.rejected);resetInfo[3]=storeMissed;
  resetInfo[4]=unsigned(maxServiceUs);resetInfo[5]=cs.inputHigh;resetInfo[6]=cs.outputHigh;resetInfo[7]=store.stats.maxBlockMs;
  resetInfo[8]=store.stats.next;resetInfo[9]=SPI.erases;resetInfo[10]=SPI.violations;resetInfo[11]=unsigned(reason);
  for(unsigned i=0;i<4;++i){resetInfo[12+i]=unsigned(offered[i]);resetInfo[16+i]=unsigned(store.stats.committed[i]);}
  resetInfo[20]=unsigned(maxCycleUs);resetInfo[21]=store.stats.full;resetInfo[22]=usbSize;resetInfo[23]=SPI.programs;
}
// Reset, then a paced recording whose every new sector is a stale sector erased lazily with the modelled erase time.
uint32_t pacedErase=20,pacedSeconds=30,pacedNoise=1; // Set by the host before the call.
extern "C" unsigned resetPaced(unsigned){
  const unsigned eraseMs=pacedErase,seconds=pacedSeconds,noise=pacedNoise;
  bootDevice();SPI.eraseDelayMs=eraseMs;noiseEnabled=noise;
  SerialConsole::handleCommand("RECORD RESET");
  if(!has("WARNING: RECORD RESET"))return 1;
  SerialConsole::handleCommand("RECORD RESET CONFIRM");
  if(!resetting)return 2;
  unsigned n=0;for(;n<4000000 && resetting;++n)cycle();
  if(resetting || !has("RESET complete") || SPI.violations || memcmp(SPI.memory,"TPR3",4))return 3;
  resetInfo[30]=n;resetInfo[31]=SPI.erases;
  waitIdle();duration(seconds);
  if(!startRecording())return 4;
  const unsigned erasesBefore=SPI.erases;
  feed(seconds);run(100);waitIdle();
  recordMetrics();resetInfo[24]=SPI.erases-erasesBefore;
  if(!store.stats.finished)return 5;
  if(!exportAll())return Tcb3Session::exporting()?7:6;
  resetInfo[22]=usbSize;
  return 0;
}
// Real serial-command sequence. stage 1: arming, timeout, busy refusals, reset. stage 2: also records afterwards.
extern "C" unsigned resetCommands(unsigned stage){
  bootDevice();noiseEnabled=0;
  SerialConsole::handleCommand("RECORD RESET CONFIRM");run(5);
  if(!has("REFUSED: send RECORD RESET first") || SPI.erases || SPI.programs || resetting)return 1;
  clearOutput();SerialConsole::handleCommand("RECORD RESET");run(5);
  if(!has("WARNING: RECORD RESET hides ALL") || !has("30 seconds") || SPI.erases || SPI.programs || resetting)return 2;
  advanceUs(31000000);clearOutput();
  SerialConsole::handleCommand("RECORD RESET CONFIRM");run(5);
  if(!has("REFUSED: send RECORD RESET first") || SPI.erases || SPI.programs || resetting)return 3;
  // Busy: a running recording refuses both the arming and the confirmation; the flash is untouched.
  duration(3);if(!startRecording())return 4;
  run(300);clearOutput();SerialConsole::handleCommand("RECORD RESET");run(50);
  if(!has("REFUSED")){return 5;}
  if(has("WARNING") || resetArmed)return 25;
  SerialConsole::handleCommand("RECORD RESET CONFIRM");run(5);
  if(resetting || memcmp(SPI.memory,"TPH3",4))return 6;
  if(!feed(3))return 7;
  run(100);waitIdle();
  // Busy: an export in flight swallows commands entirely and does not arm.
  if(!exportAll())return 8;
  resetInfo[24]=usbSize;
  clearOutput();captureUsb=true;SerialConsole::handleCommand("TCB READ");run(5);
  SerialConsole::handleCommand("RECORD RESET");
  const bool armedDuringExport=resetArmed;
  for(unsigned n=0;n<600000 && Tcb3Session::exporting();++n)cycle();
  captureUsb=false;
  if(armedDuringExport || resetting)return 9;
  // A fresh arming is needed after the mid-export attempt; wait out the GPS UART scan, which legitimately refuses a reset.
  if(!settle())return 26;
  clearOutput();SerialConsole::handleCommand("RECORD RESET");liveRun(5);
  if(!has("WARNING") || !resetArmed)return 10;
  SerialConsole::handleCommand("RECORD RESET CONFIRM");liveRun(2);
  if(!resetting)return 11;
  clearOutput();SerialConsole::handleCommand("TCB START");run(3);
  if(pending || owned || !has("REFUSED"))return 12; // The store is not usable while the reset owns the flash.
  unsigned n=0;for(;n<4000000 && resetting;++n)cycle();
  run(10);
  if(resetting || !has("RESET complete") || SPI.violations || memcmp(SPI.memory,"TPR3",4))return 13;
  for(unsigned a=0xfff000;a<0x1000000;++a)if(SPI.memory[a]!=0xa5)return 14;
  waitIdle();clearOutput();SerialConsole::handleCommand("RECORD READ");run(10);
  unsigned lines=0;const char* end=strstr(Serial.output,"END_RECORD_CSV");
  if(end)for(const char* c=Serial.output;c<end;++c)if(*c=='\n')++lines;
  if(!end || lines!=2)return 15; // Header only: V1 export is hidden once a marker exists.
  if(!exportAll())return 16;
  resetInfo[25]=usbSize;
  if(stage>=2){
    duration(2);if(!startRecording())return 17;
    if(!feed(2))return 18;
    run(100);waitIdle();recordMetrics();
    if(!exportAll())return 19;
    resetInfo[22]=usbSize;
  }
  return SPI.violations?20:0;
}
// A latched fault refuses RECORD RESET until reboot.
extern "C" unsigned resetLocked(unsigned){
  bootDevice();noiseEnabled=0;duration(3);
  if(!startRecording())return 1;
  feed(3,1000000);run(100);
  if(!locked)return 2;
  clearOutput();SerialConsole::handleCommand("RECORD RESET");run(5);
  if(!has("REFUSED") || resetArmed)return 3;
  SerialConsole::handleCommand("RECORD RESET CONFIRM");run(5);
  return resetting || SPI.erases?4:0;
}
// Power is cut `cut` main-loop passes after the confirmed reset started: flash retained by the caller.
extern "C" unsigned resetCut(unsigned cut){
  bootDevice();
  SerialConsole::handleCommand("RECORD RESET");SerialConsole::handleCommand("RECORD RESET CONFIRM");
  if(!resetting)return 1;
  unsigned n=0;for(;n<cut && resetting;++n)cycle();
  resetInfo[30]=n;resetInfo[31]=resetting;
  return 0;
}
// Reboot after a cut. mode 0 classifies the flash by attempting a normal start; mode 1 recovers with a repeated reset.
extern "C" unsigned resetRecover(unsigned mode){
  bootDevice();noiseEnabled=0;
  const unsigned kind=!memcmp(SPI.memory,"TPH3",4)?0:!memcmp(SPI.memory,"TPR3",4)?2:1;
  resetInfo[28]=kind;
  if(mode==0){
    duration(2);const bool ok=startRecording();
    resetInfo[26]=ok;resetInfo[27]=locked;resetInfo[29]=SPI.programs;
    if(ok){resetInfo[32]=store.initialAddress();feed(2);run(100);recordMetrics();settle();exportAll();resetInfo[22]=usbSize;}
    return SPI.violations?1:0;
  }
  SerialConsole::handleCommand("RECORD RESET");SerialConsole::handleCommand("RECORD RESET CONFIRM");
  if(!resetting)return 2;
  for(unsigned n=0;n<4000000 && resetting;++n)cycle();
  run(10);
  if(resetting || !has("RESET complete"))return 3;
  waitIdle();duration(2);
  if(!startRecording())return 4;
  if(!feed(2))return 5;
  run(100);recordMetrics();settle();
  if(!exportAll())return 6;
  resetInfo[22]=usbSize;
  return SPI.violations?7:0;
}
// USB behaviour. 0: 256-byte FIFO. 1: 31-byte room. 2: random room with console noise. 3: partly sent status line then TCB READ.
extern "C" unsigned usbCase(unsigned mode){
  bootDevice();
  if(mode==3){
    Serial.freeSpace=40;clearOutput();captureUsb=true;
    SerialConsole::handleCommand("TCB STATUS");run(3);
    if(!lineAt || lineAt>=lineSize)return 1;
    const unsigned at=lineAt,size=lineSize;
    SerialConsole::handleCommand("TCB STATUS");run(1); // Must not splice a new report into the unfinished one.
    if(lineSize!=size || lineAt<at)return 2;
    Serial.freeSpace=31;
    SerialConsole::handleCommand("TCB READ");
    if(!Tcb3Session::exporting())return 3;
    for(unsigned n=0;n<600000 && Tcb3Session::exporting();++n)cycle();
    captureUsb=false;resetInfo[22]=usbSize;
    return Tcb3Session::exporting() || Serial.overfilledWrites?4:0;
  }
  Serial.freeSpace=mode==1?31:256;
  const bool done=exportAll(900000,mode==2);
  resetInfo[22]=usbSize;
  return done && !Serial.overfilledWrites?0:1;
}
// DIAG ID after a latched fault. 0: flash idle. 1: flash still busy, then idle. 2: fault line partly sent when DIAG ID arrives.
extern "C" unsigned diagAfterFault(unsigned mode){
  bootDevice();noiseEnabled=0;duration(3);
  if(!startRecording())return 1;
  if(mode==2)Serial.freeSpace=40;
  feed(3,1000000);
  if(!locked)return 2;
  if(mode==2){
    run(2);if(!lineAt)return 3; // Fault line is still being sent in small pieces.
    clearOutput();
    SerialConsole::handleCommand("DIAG ID");run(30);
    if(has("BEGIN_FLASH_ID") && lineAt)return 4; // Identity text must not be spliced into a half-sent line.
    Serial.freeSpace=256;
  }else Serial.freeSpace=256;
  run(10);liveRun(40000);clearOutput();
  if(mode==1){
    SPI.busyUntil=mockTime+500;
    SerialConsole::handleCommand("DIAG ID");run(20);
    if(!has("DIAG ID refused") || has("BEGIN_FLASH_ID"))return 5;
    run(8000);clearOutput();
  }
  // A refusal is allowed while the compressor/flash drains after the fault; it must clear within a bounded time.
  unsigned attempts=0;const unsigned programs0=SPI.programs,erases0=SPI.erases;
  for(;attempts<40;++attempts){
    clearOutput();SerialConsole::handleCommand("DIAG ID");run(20);
    if(has("BEGIN_FLASH_ID") || identityRunning)break;
    liveRun(5000);
  }
  resetInfo[26]=attempts;
  if(attempts>=40)return 6;
  for(unsigned n=0;n<200000 && !has("END_FLASH_ID");++n)cycle();
  if(!has("BEGIN_FLASH_ID") || !has("END_FLASH_ID") || has("DIAG ID refused"))return 10;
  // Every text line is whole: nothing spliced, nothing truncated mid-line.
  const char* p=Serial.output;
  while(*p){
    const char* e=strchr(p,'\n');if(!e)return 7;
    if(strncmp(p,"TCB ",4) && strncmp(p,"BEGIN_FLASH_ID",14) && strncmp(p,"FLASH ID",8) && strncmp(p,"END_FLASH_ID",12) &&
       strncmp(p,"DIAG",4) && strncmp(p,"JEDEC",5) && strncmp(p,"Flash",5))return 8;
    p=e+1;
  }
  // The generic NOR mock does not know the REMS opcode used by DIAG ID (covered by tests/flash_identity), so check read-only behaviour instead.
  return SPI.programs!=programs0 || SPI.erases!=erases0?9:0;
}
