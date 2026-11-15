#include "fast_discovery_harness.cpp"
#include "../../src/tcb3_fast_discovery.h"

Tcb3FastDiscovery::Scanner production;
uint32_t productionData[29],writeCalls=0,maxReadsPerStep=0,maxStatusPerStep=0;
bool deniedBegin(void*,uint32_t){++writeCalls;return false;}
bool deniedBytes(void*,const uint8_t*,unsigned){++writeCalls;return false;}
bool deniedEnd(void*){++writeCalls;return false;}
void deniedAbort(void*){++writeCalls;}
bool deniedProgram(void*,uint32_t,const uint8_t*,unsigned){++writeCalls;return false;}
bool deniedErase(void*,uint32_t){++writeCalls;return false;}

extern "C" unsigned productionBegin(unsigned end){
  flash.read=countedRead;flash.beginPage=deniedBegin;flash.pageBytes=deniedBytes;
  flash.endPage=deniedEnd;flash.abortPage=deniedAbort;flash.program=deniedProgram;flash.erase=deniedErase;
  readBytes=readCalls=scanSteps=writeCalls=maxReadsPerStep=maxStatusPerStep=exportStatusPolls=0;
  production.begin(flash,end);return production.done();
}
extern "C" unsigned productionStep(unsigned budget){
  while(budget-- && !production.done()){
    const uint32_t before=readCalls,statusBefore=exportStatusPolls;
    production.update(true);++scanSteps;
    const uint32_t reads=readCalls-before,statuses=exportStatusPolls-statusBefore;
    if(reads>maxReadsPerStep)maxReadsPerStep=reads;
    if(statuses>maxStatusPerStep)maxStatusPerStep=statuses;
  }
  return production.done();
}
extern "C" unsigned productionPause(unsigned times){
  while(times--)production.update(false);
  return production.done();
}
extern "C" unsigned productionResult(){return unsigned(production.result());}
extern "C" unsigned productionMissingPort(unsigned missing){
  Tcb3::Port invalid=flash;
  if(missing==1)invalid.read=nullptr;
  if(missing==2)invalid.status=nullptr;
  production.begin(invalid,Tcb3::End);
  return unsigned(production.result());
}
extern "C" unsigned productionCount(){return production.count();}
extern "C" unsigned productionIncomplete(){return production.incomplete();}
extern "C" unsigned productionOverflow(){return production.overflow();}
extern "C" unsigned productionSize(){return sizeof(production);}
extern "C" unsigned productionEntry(unsigned index){
  const auto* c=production.entry(index);if(!c)return 0;
  const auto& e=c->metadata;
  memset(productionData,0,sizeof(productionData));
  productionData[0]=e.sessionId;productionData[1]=e.resetEpoch;productionData[2]=e.firstAddress;
  productionData[3]=e.sectorCount;productionData[4]=e.formatVersion;productionData[5]=e.flags;
  productionData[6]=e.footerReason;productionData[7]=uint32_t(e.startUs);
  productionData[8]=uint32_t(e.startUs>>32);productionData[9]=uint32_t(e.stopUs);
  productionData[10]=uint32_t(e.stopUs>>32);productionData[11]=uint32_t(e.deviceId);
  productionData[12]=uint32_t(e.deviceId>>32);
  for(unsigned i=0;i<4;++i){
    productionData[13+2*i]=uint32_t(e.committed[i]);productionData[14+2*i]=uint32_t(e.committed[i]>>32);
    productionData[21+2*i]=uint32_t(e.lost[i]);productionData[22+2*i]=uint32_t(e.lost[i]>>32);
  }
  return c->provisional;
}
