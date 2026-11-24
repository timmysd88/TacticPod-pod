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
    if(!Recorder::faulted() || strcmp(Recorder::startupFaultReason(),"unsupported flash JEDEC identity"))return 6;
    Serial.used=0;Serial.output[0]=0;Recorder::handleCommand("RECORD STATUS");
    if(!strstr(Serial.output,"startup_fault=\"unsupported flash JEDEC identity\""))return 7;
    Serial.used=0;Serial.output[0]=0;
    if(!SerialConsole::handleCommand("DIAG ID") || !identityRequested)return 8;
    for(unsigned i=0;i<200;++i){loop();++mockTime;}
    if(!strstr(Serial.output,"BEGIN_FLASH_ID") || !strstr(Serial.output,"END_FLASH_ID"))return 9;
  }else if(scenario==3){
    SPI.stuckBusy=true;Recorder::begin(true);tick(5100);
    if(!Recorder::faulted() || strcmp(Recorder::startupFaultReason(),"startup busy timeout"))return 10;
    Serial.used=0;Serial.output[0]=0;
    if(!SerialConsole::handleCommand("DIAG ID"))return 11;
    for(unsigned i=0;i<20;++i){loop();++mockTime;}
    if(!strstr(Serial.output,"FLASH ID ERROR") || !strstr(Serial.output,"SR1=01"))return 12;
  }else{
    Recorder::handleCommand("RECORD START");feed(15);stop();
    uint8_t saved[4096];memcpy(saved,SPI.memory,sizeof(saved));
    memset(SPI.sfdp,0xff,sizeof(SPI.sfdp));
    const uint8_t header[]={0x53,0x46,0x44,0x50,0,1,0,0xff,0,0,1,9,0x80,0,0,0xff};
    const uint8_t table[]={0xe5,0x20,0xf1,0xff,0xff,0xff,0xff,7};
    memcpy(SPI.sfdp,header,sizeof(header));memcpy(SPI.sfdp+128,table,sizeof(table));
    SPI.sfdp[156]=12;SPI.sfdp[157]=0x20;
    SPI.winbond=true;FlashSupport::active()=FlashSupport::Manufacturer::Unknown;
    rebootRecorder();
    if(Recorder::faulted() || nextSector!=4096)return 13;
    Recorder::handleCommand("RECORD START");feed(20);stop();
    if(committed!=20 || !validAt(4096) || memcmp(saved,SPI.memory,sizeof(saved)) || SPI.erases)return 14;
    Serial.used=0;Serial.output[0]=0;Recorder::handleCommand("RECORD READ");tick(1000);
    if(!strstr(Serial.output,"END_RECORD_CSV") || !strstr(Serial.output,"2,"))return 15;
    return SPI.violations?16:0;
  }
  return SPI.enables || SPI.programs || SPI.erases || SPI.violations?5:0;
}
