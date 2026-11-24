#include "../recording/harness.cpp"
extern "C" unsigned identityRecordingCase(unsigned scenario){
  memset(SPI.memory,255,sizeof(SPI.memory));memset(SPI.finalSector,255,sizeof(SPI.finalSector));
  rebootRecorder();flashReportPrinted=true;
  if(scenario==0){
    Recorder::handleCommand("RECORD READ");if(!Recorder::exporting())return 1;
    unsigned before=Serial.used;
    if(!SerialConsole::handleCommand("DIAG ID") || Serial.used!=before || identityRequested)return 2;
  }else if(scenario==1){
    Recorder::handleCommand("RECORD START");if(!Recorder::recording())return 3;
    if(!SerialConsole::handleCommand("DIAG ID") || identityRequested || !strstr(Serial.output,"DIAG ID refused"))return 4;
  }else if(scenario==2){
    SPI.wrongId=true;rebootRecorder();
    if(!Recorder::faulted() || strcmp(Recorder::startupFaultReason(),"expected JEDEC 0B 40 18 not found"))return 6;
    Serial.used=0;Serial.output[0]=0;Recorder::handleCommand("RECORD STATUS");
    if(!strstr(Serial.output,"startup_fault=\"expected JEDEC 0B 40 18 not found\""))return 7;
    Serial.used=0;Serial.output[0]=0;
    if(!SerialConsole::handleCommand("DIAG ID") || !identityRequested)return 8;
    for(unsigned i=0;i<200;++i){loop();++mockTime;}
    if(!strstr(Serial.output,"BEGIN_FLASH_ID") || !strstr(Serial.output,"END_FLASH_ID"))return 9;
  }else{
    SPI.stuckBusy=true;Recorder::begin(true);tick(5100);
    if(!Recorder::faulted() || strcmp(Recorder::startupFaultReason(),"startup busy timeout"))return 10;
    Serial.used=0;Serial.output[0]=0;
    if(!SerialConsole::handleCommand("DIAG ID"))return 11;
    for(unsigned i=0;i<20;++i){loop();++mockTime;}
    if(!strstr(Serial.output,"FLASH ID ERROR") || !strstr(Serial.output,"SR1=01"))return 12;
  }
  return SPI.enables || SPI.programs || SPI.erases || SPI.violations?5:0;
}
