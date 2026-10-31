// Review-only: complete 16 MiB RAM NOR, never a physical SPI device.
#define TCB_SIM_FLASH_BYTES 0x1000000
#include "harness.cpp"
extern "C" unsigned eraseReview(unsigned scenario){
  memset(SPI.memory,0xa5,sizeof(SPI.memory));
  memset(SPI.finalSector,0xa5,sizeof(SPI.finalSector));
  rebootRecorder();
  if(stage!=Stage::Idle || Recorder::recording())return 1;
  Recorder::handleCommand("RECORD ERASE");
  if(SPI.erases || SPI.enables || SPI.programs)return 2;
  if(scenario==1)SPI.rejectErase=true;
  if(scenario==2)SPI.denyWel=true;
  if(scenario==3)SPI.wrongId=true;
  Recorder::handleCommand("RECORD ERASE CONFIRM");
  unsigned observed=0;
  for(unsigned ticks=0;ticks<300000 && stage!=Stage::Idle && stage!=Stage::Fault;++ticks){
    ++mockTime;Recorder::update();
    if(SPI.erases!=observed){
      if(SPI.erases!=observed+1 || SPI.tx[0]!=0x20 || SPI.address()!=observed*4096 || SPI.address()>=0xfff000)return 3;
      ++observed;
      if(scenario==4)SPI.wel=true; // Simulate a command that never clears WEL.
    }
  }
  if(scenario){
    for(unsigned a=0xfff000;a<0x1000000;++a)if(SPI.memory[a]!=0xa5)return 9;
    unsigned expected=scenario==1 || scenario==4?1:0;
    return stage==Stage::Fault && observed==expected && !SPI.programs && !Recorder::recording()?0:10;
  }
  if(stage!=Stage::Idle || observed!=4095 || SPI.enables!=4095 || SPI.programs || SPI.violations)return 4;
  if(nextSector || Recorder::recording() || io!=IO::None)return 5;
  for(unsigned a=0;a<0xfff000;++a)if(SPI.memory[a]!=255)return 6;
  for(unsigned a=0xfff000;a<0x1000000;++a)if(SPI.memory[a]!=0xa5)return 7;
  if(!strstr(Serial.output,"RECORD erase complete; diagnostic sector untouched. Recording remains OFF."))return 8;
  return 0;
}
