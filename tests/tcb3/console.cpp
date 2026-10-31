#define TACTICPOD_TCB3_FLASH_ENABLED 1
#define TCB_LARGE_TEST_NOR 1
#include "../flash_write/harness.cpp"
#include "../../src/tcb3_store.cpp"
#include "../../src/tcb3_session.cpp"
extern "C" unsigned consoleCase(unsigned unused){
  (void)unused;
  memset(SPI.memory,255,sizeof(SPI.memory));memset(SPI.finalSector,255,sizeof(SPI.finalSector));
  Serial.connected=false;setup();Serial.connected=true;Serial.rx="";FlashWriteTest::update();
  Serial.rx="TCB START\nTCB STOP\n";loop();
  if(Tcb3Session::exclusive() || TcbDetail::owned || CompactRecorder::capturing() || SPI.programs)return 4;
  Serial.rx="TCB START\n";
  for(unsigned n=0;n<30000 && !TcbDetail::owned;++n){loop();++mockTime;}
  if(!TcbDetail::owned)return 1;
  Serial.rx="TCB STATUS\nCOMP CLEAR\nOBS CLEAR\nRECORD START\nERASE FFF000\n";
  for(unsigned n=0;n<1000;++n){loop();++mockTime;}
  if(!CompactRecorder::capturing() || SPI.erases || SPI.violations || state!=State::Idle)return 2;
  Serial.rx="TCB STOP\n";
  for(unsigned n=0;n<30000 && TcbDetail::owned;++n){loop();++mockTime;}
  if(TcbDetail::owned || TcbDetail::locked || CompactRecorder::capturing() || SPI.erases || SPI.violations)return 3;
  return 0;
}
