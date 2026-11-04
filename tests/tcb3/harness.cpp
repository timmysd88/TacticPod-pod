#include <stdint.h>
#include <string.h>
#include "../../src/tcb3_store.cpp"
uint8_t nor[0x1000000]; // Host model backing store, NOT firmware RAM.
uint8_t input[8228],metadataInput[160];
uint32_t nowMs=0,programs=0,reads=0,violations=0,busyUntil=0,delayMs=3;
uint32_t deny=0,stuck=0,corrupt=0,tearCall=0,tearBytes=0;
uint32_t modelErases=0;
uint32_t corruptCommit=0;
uint32_t denyRead=0;
uint8_t pageProgramCounts[65536];uint32_t partialLimit=0;
uint32_t traceAddress[10000],traceLength[10000];uint8_t traceBytes[10000][256];
uint8_t staged[256];uint32_t stagedAt=0,stagedSize=0;bool stagedOpen=false;
Tcb3::Store engine;Tcb3::Export exporter;
uint8_t exportPacket[280];uint32_t info[32];
bool modelRead(void*,uint32_t a,uint8_t* p,unsigned n){
  if(stagedOpen){++violations;return false;}
  if(denyRead)return false;
  ++reads;if(!n || n>64 || a>=Tcb3::End || n>Tcb3::End-a){++violations;return false;}
  memcpy(p,nor+a,n);return true;
}
int modelStatus(void*){if(stagedOpen){++violations;return -1;}return (stuck || int32_t(nowMs-busyUntil)<0)?1:0;}
bool modelProgram(void*,uint32_t a,const uint8_t* p,unsigned n){
  if(deny)return false;
  if(!n || n>256 || a>=Tcb3::End || n>Tcb3::End-a || (a&255)+n>256 || modelStatus(nullptr)) {++violations;return false;}
  if(n==4 && (a&255)==252){
    uint32_t base=a&~255U;
    if(Tcb3::get(nor+base+248)!=~Tcb3::crc32(nor+base,248) || Tcb3::get(nor+a)!=0xffffffffU || Tcb3::get(p)!=0xc0dec0deU){++violations;return false;}
  }
  if(partialLimit && pageProgramCounts[a/256]>=partialLimit)return false;
  if(pageProgramCounts[a/256]<255)++pageProgramCounts[a/256];
  if(programs<10000){traceAddress[programs]=a;traceLength[programs]=n;memcpy(traceBytes[programs],p,n);}
  ++programs;unsigned actual=programs==tearCall && tearBytes<n?tearBytes:n;
  for(unsigned i=0;i<actual;++i){if((nor[a+i]&p[i])!=p[i]){++violations;return false;}nor[a+i]&=p[i];}
  if(corrupt && actual)nor[a]^=1;
  if(corruptCommit && n==256)nor[a&~255U]^=1;
  busyUntil=nowMs+delayMs;return true;
}
bool modelBegin(void*,uint32_t a){
  if(stagedOpen || (a&255) || a>=Tcb3::End || 256>Tcb3::End-a || modelStatus(nullptr)){++violations;return false;}
  stagedAt=a;stagedSize=0;stagedOpen=true;return true;
}
bool modelBytes(void*,const uint8_t* p,unsigned n){
  if(!stagedOpen || !n || n>64 || stagedSize+n>256){++violations;return false;}
  memcpy(staged+stagedSize,p,n);stagedSize+=n;return true;
}
bool modelEnd(void*){if(!stagedOpen || stagedSize!=256){++violations;return false;}stagedOpen=false;return modelProgram(nullptr,stagedAt,staged,256);}
void modelAbort(void*){if(stagedOpen){stagedOpen=false;if(stagedSize)modelProgram(nullptr,stagedAt,staged,stagedSize);}}
uint32_t eraseCalls=0,eraseTearCall=0,eraseTrace[10000],eraseTraceCount=0;
Tcb3::Reset resetter;
// Torn erase model: the first tearBytes bytes become blank, the rest is left as partly erased garbage.
bool modelErase(void*,uint32_t a){
  if((a&4095) || a>=Tcb3::End || stagedOpen || modelStatus(nullptr)){++violations;return false;}
  ++eraseCalls;if(eraseTraceCount<10000)eraseTrace[eraseTraceCount++]=a;
  if(eraseTearCall && eraseCalls==eraseTearCall){
    for(unsigned i=0;i<4096;++i)nor[a+i]=i<tearBytes?255:(nor[a+i]|0x3c);
  }else{memset(nor+a,255,4096);memset(pageProgramCounts+a/256,0,16);}
  busyUntil=nowMs+20;++modelErases;return true;
}
Tcb3::Port model{nullptr,modelRead,modelBegin,modelBytes,modelEnd,modelAbort,modelStatus,modelProgram,modelErase};
extern "C" unsigned init(unsigned erase){
  if(erase){memset(nor,255,sizeof(nor));memset(pageProgramCounts,0,sizeof(pageProgramCounts));}
  nowMs=programs=reads=violations=busyUntil=deny=stuck=corrupt=tearCall=tearBytes=modelErases=corruptCommit=denyRead=partialLimit=0;delayMs=3;
  eraseCalls=eraseTearCall=eraseTraceCount=0;resetter=Tcb3::Reset{};
  stagedOpen=false;stagedSize=0;engine=Tcb3::Store{};engine.begin(model,nowMs);return 0;
}
extern "C" unsigned resetStart(unsigned unused){(void)unused;resetter.begin(model,nowMs);return 0;}
// 1=running, 2=done, 3=failed.
extern "C" unsigned resetTick(unsigned n){
  while(n-- && resetter.active()){++nowMs;resetter.update(nowMs);}
  return resetter.done()?2:resetter.failed()?3:1;
}
extern "C" unsigned tick(unsigned n){
  while(n--){++nowMs;engine.update(nowMs);}
  info[0]=unsigned(engine.state());info[1]=engine.stats.next;info[2]=engine.stats.session;
  info[3]=engine.stats.blocks;info[4]=engine.stats.pages;info[5]=engine.stats.full;info[6]=engine.stats.crcFailures;
  info[7]=engine.stats.incompleteWrites;info[8]=engine.stats.finished;info[9]=engine.released();
  info[21]=engine.stats.programOperations;info[22]=engine.stats.maxBlockMs;
  return info[0];
}
extern "C" unsigned start(unsigned unused){(void)unused;Tcb3::metadata(metadataInput,1000);return engine.start(metadataInput);}
extern "C" unsigned cancel(unsigned unused){(void)unused;return engine.cancelScan();}
extern "C" unsigned resetHeld(unsigned releaseCs){if(releaseCs)modelAbort(nullptr);return init(0);}
extern "C" unsigned eraseModel(unsigned a){
  if((a&4095) || a>=Tcb3::End || modelStatus(nullptr)){++violations;return 0;}
  memset(nor+a,255,4096);memset(pageProgramCounts+a/256,0,16);busyUntil=nowMs+20;++modelErases;return 1;
}
extern "C" unsigned programModel(unsigned a){return modelProgram(nullptr,a,input,info[20]);}
extern "C" unsigned offer(unsigned n){return engine.offer(input,n);}
uint64_t offeredCounts[4]{};
extern "C" unsigned stop(unsigned reason){engine.finish(uint64_t(nowMs)*1000,offeredCounts,Tcb3::Reason(reason),0,0);return 0;}
extern "C" unsigned exporting(unsigned begin){
  if(begin)exporter.begin(model);
  if(exporter.done())return 0;
  for(unsigned i=0;i<5000;++i){
    exporter.update();unsigned n=0;const uint8_t* p=exporter.output(n);
    if(p){memcpy(exportPacket,p,n);exporter.release();return n;}
    if(exporter.done())return 0;
  }return 0xffffffffU;
}

// Maximum-frame producer: one independently checked raw block every 2000ms.
// Two output leases model the real bounded queue. A configurable 400ms service
// blackout reserves CPU time for compression, not a claimed MCU WCET.
uint8_t sustainedFrames[2][8228];uint32_t sustainedInfo[16]{};
extern "C" unsigned sustain(unsigned mode){
  unsigned period=2000,blackout=mode&1?400:0;delayMs=(mode&2)?10:3;
  partialLimit=1;
  Tcb3::metadata(metadataInput,uint64_t(nowMs)*1000);
  if(!engine.start(metadataInput))return 1;
  while(engine.state()!=Tcb3::State::Ready && engine.state()!=Tcb3::State::Fault){++nowMs;engine.update(nowMs);}
  uint32_t start=nowMs,next=0,produced=0,consumed=0,latencyMax=0,queueHigh=0;
  unsigned head=0,count=0;bool leased=false;uint32_t arrivals[2]{};
  uint64_t counts[4]{};
  for(unsigned ticks=0;ticks<4000000;++ticks){
    uint32_t elapsed=nowMs-start;
    if(elapsed>=next && !engine.stats.full){
      if(count==2)return 2;
      unsigned slot=(head+count)%2;uint8_t* p=sustainedFrames[slot];memset(p,0,8228);
      memcpy(p,"TCB3",4);Tcb3::put(p+8,produced);Tcb3::put(p+12,8192,2);Tcb3::put(p+14,8192,2);
      Tcb3::put(p+22,256,2);uint64_t base=uint64_t(nowMs)*1000;Tcb3::put(p+24,base,8);
      for(unsigned i=0;i<256;++i){uint8_t* r=p+32+i*32;r[0]=4;r[1]=1;Tcb3::put(r+2,32,2);Tcb3::put(r+4,base+i,8);
        Tcb3::put(r+12,1,2);r[14]=0;r[15]=1;Tcb3::put(r+16,produced);Tcb3::put(r+20,i);Tcb3::put(r+24,UINT64_MAX-i-produced,8);}
      Tcb3::put(p+8224,~Tcb3::crc32(p,8224));arrivals[slot]=nowMs;++count;++produced;counts[3]+=256;next+=period;
      if(count>queueHigh)queueHigh=count;
    }
    if(!leased && count && engine.accepting()){
      if(!engine.offer(sustainedFrames[head],8228))return 3;leased=true;
    }
    // Reserve all flash service in the first blackout ms of each period. This
    // is deliberately more pessimistic than interleaved compressor slices.
    if(elapsed%period>=blackout)engine.update(nowMs);
    if(engine.state()==Tcb3::State::Fault)return 4;
    if(engine.released()){
      if(!leased || !count)return 5;
      uint32_t latency=nowMs-arrivals[head];if(latency>latencyMax)latencyMax=latency;
      if(!engine.stats.full)++consumed;
      head=(head+1)%2;--count;leased=false;
    }
    if(engine.stats.full){
      engine.finish(uint64_t(nowMs)*1000,counts,Tcb3::Reason::Full,0,0);
      while(engine.state()!=Tcb3::State::Idle && engine.state()!=Tcb3::State::Fault){++nowMs;engine.update(nowMs);}
      sustainedInfo[0]=produced;sustainedInfo[1]=consumed;sustainedInfo[2]=queueHigh;sustainedInfo[3]=latencyMax;
      sustainedInfo[4]=nowMs-start;sustainedInfo[5]=engine.stats.maxBlockMs;sustainedInfo[6]=programs;
      sustainedInfo[7]=violations;sustainedInfo[8]=engine.stats.next;sustainedInfo[9]=engine.stats.full;
      sustainedInfo[10]=blackout;sustainedInfo[11]=delayMs;return engine.state()==Tcb3::State::Idle?0:6;
    }
    ++nowMs;
  }return 7;
}
