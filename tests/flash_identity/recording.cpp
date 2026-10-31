#include "../recording/harness.cpp"
extern "C" unsigned identityRecordingCase(unsigned scenario){
  memset(SPI.memory,255,sizeof(SPI.memory));memset(SPI.finalSector,255,sizeof(SPI.finalSector));
  rebootRecorder();flashReportPrinted=true;
  if(scenario==0){
    Recorder::handleCommand("RECORD READ");if(!Recorder::exporting())return 1;
    unsigned before=Serial.used;
    if(!SerialConsole::handleCommand("DIAG ID") || Serial.used!=before || identityRequested)return 2;
  }else{
    Recorder::handleCommand("RECORD START");if(!Recorder::recording())return 3;
    if(!SerialConsole::handleCommand("DIAG ID") || identityRequested || !strstr(Serial.output,"DIAG ID refused"))return 4;
  }
  return SPI.enables || SPI.programs || SPI.erases || SPI.violations?5:0;
}
