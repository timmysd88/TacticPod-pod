#define TACTICPOD_REUSABLE 1
#define TACTICPOD_TCB3_FLASH_ENABLED 1
#include "../tcb3/harness.cpp"

// Short synthetic recording: `blocks` maximum-size raw blocks, clean finish. Returns 0 when Idle and finished.
extern "C" unsigned record(unsigned blocks){
  Tcb3::metadata(metadataInput,uint64_t(nowMs)*1000);
  if(!engine.start(metadataInput))return 1;
  uint64_t counts[4]{};unsigned produced=0;bool leased=false,closing=false;
  for(unsigned ticks=0;ticks<20000000;++ticks){
    if(engine.state()==Tcb3::State::Fault)return 4;
    if(!leased && !closing && engine.accepting()){
      if(produced==blocks){engine.finish(uint64_t(nowMs)*1000,counts,Tcb3::Reason::Clean,0,0);closing=true;}
      else{
        uint8_t* p=input;memset(p,0,8228);
        memcpy(p,"TCB3",4);Tcb3::put(p+8,produced);Tcb3::put(p+12,8192,2);Tcb3::put(p+14,8192,2);
        Tcb3::put(p+22,256,2);uint64_t base=uint64_t(nowMs)*1000;Tcb3::put(p+24,base,8);
        for(unsigned i=0;i<256;++i){uint8_t* r=p+32+i*32;r[0]=4;r[1]=1;Tcb3::put(r+2,32,2);Tcb3::put(r+4,base+i,8);
          Tcb3::put(r+12,1,2);r[14]=0;r[15]=1;Tcb3::put(r+16,produced);Tcb3::put(r+20,i);Tcb3::put(r+24,UINT64_MAX-i-produced,8);}
        Tcb3::put(p+8224,~Tcb3::crc32(p,8224));
        if(!engine.offer(p,8228))return 3;leased=true;++produced;counts[3]+=256;
      }
    }
    ++nowMs;engine.update(nowMs);
    if(engine.released())leased=false;
    if(engine.stats.finished && engine.state()==Tcb3::State::Idle)return 0;
  }
  return 7;
}
