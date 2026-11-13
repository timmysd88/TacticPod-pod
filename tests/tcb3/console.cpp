#define TACTICPOD_TCB3_FLASH_ENABLED 1
#define TCB_LARGE_TEST_NOR 1
#include "../flash_write/harness.cpp"
#include "../../src/tcb3_store.cpp"
#include "../../src/tcb3_session.cpp"
extern "C" unsigned bulkReadCase(unsigned fast){
  using namespace TcbDetail;
  uint8_t data[66];
  for(unsigned i=0;i<64;++i)SPI.memory[0x1234+i]=uint8_t(i*37);
  dumping=fast;
  const unsigned sizes[]={1,31,64};
  for(unsigned n: sizes){
    memset(data,0xa5,sizeof(data));
    const unsigned before=SPI.bulkReads;
    if(!read(nullptr,0x1234,data+1,n))return 1;
    if(SPI.bulkReads!=before+1 || SPI.position!=n+4 || SPI.address()!=0x1234 ||
       SPI.hz!=(fast?4000000U:1000000U) || SPI.transactionActive || SPI.violations)return 2;
    if(data[0]!=0xa5 || data[n+1]!=0xa5 || memcmp(data+1,SPI.memory+0x1234,n))return 3;
  }
  const unsigned before=SPI.bulkReads;
  if(read(nullptr,0,data,0) || read(nullptr,0,data,65) ||
     read(nullptr,Tcb3::End,data,1) || read(nullptr,Tcb3::End-1,data,2))return 4;
  pageOpen=true;
  if(read(nullptr,0,data,1))return 5;
  pageOpen=false;
  return SPI.bulkReads!=before?6:0;
}
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
