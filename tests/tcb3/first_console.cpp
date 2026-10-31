#ifndef TACTICPOD_TCB3_TEST_BUILD
#define TACTICPOD_TCB3_TEST_BUILD 1
#endif
#define TACTICPOD_TCB3_FLASH_ENABLED 1
#define TCB_LARGE_TEST_NOR 1
#include "../flash_write/harness.cpp"
#include "../../src/tcb3_store.cpp"
#include "../../src/tcb3_session.cpp"
extern "C" unsigned firstConsole(){
  memset(SPI.memory,255,sizeof(SPI.memory));memset(SPI.finalSector,255,sizeof(SPI.finalSector));
  Serial.connected=false;setup();
  for(unsigned n=0;n<20000;++n){loop();++mockTime;}
  if(SPI.programs || SPI.erases || SPI.enables || Tcb3Session::exclusive() || CompactRecorder::capturing())return 1;
  Serial.connected=true;FlashWriteTest::update();
  Serial.rx="ERASE FFF000\nRECORD START\nRECORD ERASE\nRECORD ERASE CONFIRM\n";
  for(unsigned n=0;n<100;++n){loop();++mockTime;}
  startTest(); // Internal diagnostic entry point also refuses this build.
  return SPI.programs || SPI.erases || SPI.enables || state!=State::Idle || SPI.violations?2:0;
}
