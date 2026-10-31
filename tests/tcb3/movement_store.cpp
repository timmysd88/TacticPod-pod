#define TACTICPOD_TCB3_TEST_BUILD 3
#define TACTICPOD_TCB3_FLASH_ENABLED 1
#include "harness.cpp"
unsigned appendStatusBits=0;
int appendStatus(void* p){return modelStatus(p)|appendStatusBits;}
extern "C" unsigned appendStatusCase(unsigned probe){
  Tcb3::Port flagged=model;flagged.status=appendStatus;engine.begin(flagged,nowMs);
  if(probe)tick(4128);
  appendStatusBits=4;tick(1);return unsigned(engine.state());
}

// Deliberately corrupt public counters after the read-only probe or immediately
// before the first program dispatch. The engine must fault without Port::begin.
extern "C" unsigned appendGuard(unsigned mode){
  Tcb3::metadata(metadataInput,1000);
  if(mode==0){engine.stats.next=0xc000;return engine.start(metadataInput)?1:unsigned(engine.state());}
  if(!engine.start(metadataInput))return 99;
  if(mode==1){
    // Full sector check (64 slices) and full page destination check (4 slices).
    tick(68);engine.stats.next=0xd000;tick(1);
    return unsigned(engine.state());
  }
  denyRead=1;tick(1);return unsigned(engine.state());
}
