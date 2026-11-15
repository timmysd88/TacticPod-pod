#include "manifest.cpp"
#include "fast_discovery.cpp"

FastDiscovery::Scanner fast;
uint32_t readBytes=0, readCalls=0, scanSteps=0;
uint32_t candidateData[29];

bool countedRead(void* context,uint32_t address,uint8_t* output,unsigned length){
  ++readCalls;readBytes+=length;
  return readFlash(context,address,output,length);
}
extern "C" unsigned scanBegin(unsigned kind){
  flash.read=countedRead;readBytes=readCalls=scanSteps=0;
  if(kind)fast.begin(flash,Tcb3::End);else manifest.begin(flash,0,Tcb3::End);
  return kind?fast.done():manifest.failed();
}
extern "C" unsigned scanFast(unsigned budget){
  while(budget-- && !fast.done()){fast.update();++scanSteps;}
  return fast.done();
}
extern "C" unsigned scanFull(unsigned budget){
  while(budget-- && !manifest.done()){manifest.update();++scanSteps;}
  return manifest.done();
}
extern "C" unsigned fastResult(){return unsigned(fast.result());}
extern "C" unsigned fastCount(){return fast.count();}
extern "C" unsigned fastIncomplete(){return fast.incomplete();}
extern "C" unsigned fastOverflow(){return fast.overflow();}
extern "C" unsigned fastError(){return uint32_t(reinterpret_cast<uintptr_t>(fast.error()));}
extern "C" unsigned fastEntry(unsigned index){
  const auto* c=fast.entry(index);if(!c)return 0;
  const auto& e=c->metadata;
  memset(candidateData,0,sizeof(candidateData));
  candidateData[0]=e.sessionId;candidateData[1]=e.resetEpoch;candidateData[2]=e.firstAddress;
  candidateData[3]=e.sectorCount;candidateData[4]=e.formatVersion;candidateData[5]=e.flags;
  candidateData[6]=e.footerReason;candidateData[7]=uint32_t(e.startUs);
  candidateData[8]=uint32_t(e.startUs>>32);candidateData[9]=uint32_t(e.stopUs);
  candidateData[10]=uint32_t(e.stopUs>>32);candidateData[11]=uint32_t(e.deviceId);
  candidateData[12]=uint32_t(e.deviceId>>32);
  for(unsigned i=0;i<4;++i){
    candidateData[13+2*i]=uint32_t(e.committed[i]);candidateData[14+2*i]=uint32_t(e.committed[i]>>32);
    candidateData[21+2*i]=uint32_t(e.lost[i]);candidateData[22+2*i]=uint32_t(e.lost[i]>>32);
  }
  return c->provisional;
}
