#define TACTICPOD_TCB3_TEST_BUILD 1
#define TACTICPOD_TCB3_FLASH_ENABLED 1
#include "harness.cpp"
extern "C" unsigned firstFence(){
  init(1);tick(4095);partialLimit=1;memset(nor+0x10000,0xa5,sizeof(nor)-0x10000);
  // A scan is already complete; exercising the writer's independent cap directly.
  if(!start(0))return 1;
  tick(200);unsigned sequence=0;uint64_t counts[4]{};
  while(!engine.stats.full && sequence<8){
    memset(input,0,8228);memcpy(input,"TCB3",4);Tcb3::put(input+8,sequence);
    Tcb3::put(input+12,8192,2);Tcb3::put(input+14,8192,2);Tcb3::put(input+22,256,2);
    uint64_t base=1000+sequence*1000;Tcb3::put(input+24,base,8);
    for(unsigned i=0;i<256;++i){uint8_t* p=input+32+32*i;p[0]=4;p[1]=1;Tcb3::put(p+2,32,2);Tcb3::put(p+4,base+i,8);Tcb3::put(p+12,1,2);}
    Tcb3::put(input+8224,~Tcb3::crc32(input,8224));
    if(!engine.offer(input,8228))return 2;
    counts[3]+=256;
    for(unsigned n=0;n<10000 && !engine.released();++n){++nowMs;engine.update(nowMs);}
    ++sequence;
  }
  if(!engine.stats.full || engine.state()==Tcb3::State::Fault)return 3;
  engine.finish(uint64_t(nowMs)*1000,counts,Tcb3::Reason::Full,0,0);tick(200);
  if(!engine.stats.finished || engine.stats.next!=0x10000)return 4;
  for(unsigned i=0;i<programs;++i)if(traceAddress[i]>=0x10000 || traceAddress[i]+traceLength[i]>0x10000)return 5;
  for(unsigned a=0x10000;a<sizeof(nor);++a)if(nor[a]!=0xa5)return 6;
  if(violations || modelErases)return 7;
  return 0;
}
