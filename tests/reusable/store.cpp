#define TACTICPOD_REUSABLE 1
#define TACTICPOD_TCB3_FLASH_ENABLED 1
#include "../tcb3/harness.cpp"
extern "C" unsigned tamper(unsigned mode){
  Tcb3::metadata(metadataInput,1000);
  if(mode==0){engine.stats.next+=4096;engine.start(metadataInput);return unsigned(engine.state());}
  if(!engine.start(metadataInput))return 99;
  tick(68);engine.stats.sectorSequence=99;tick(1);return unsigned(engine.state());
}
