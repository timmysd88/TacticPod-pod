#include "ficr.h"
#include "../recording/harness.cpp"
char legacyIdentityCsv[262144]{};unsigned legacyIdentitySize=0;
extern "C" unsigned identityLegacy(unsigned inject){
  memset(SPI.memory,255,sizeof(SPI.memory));memset(SPI.finalSector,255,sizeof(SPI.finalSector));rebootRecorder();
  Recorder::handleCommand("RECORD START");feed(20);
  Serial.used=0;Serial.output[0]=0;
  SerialConsole::handleCommand("DEVICE INFO");SerialConsole::serviceDeviceInfo();
  if(Serial.used)return 1;
  stop();Serial.used=0;Serial.output[0]=0;Recorder::handleCommand("RECORD READ");
  for(unsigned n=0;n<1000;++n){
    if(inject && Recorder::exporting()){SerialConsole::handleCommand("DEVICE INFO");SerialConsole::serviceDeviceInfo();}
    tick();
  }
  if(!strstr(Serial.output,"END_RECORD_CSV") || strstr(Serial.output,"device_info"))return 2;
  legacyIdentitySize=Serial.used;memcpy(legacyIdentityCsv,Serial.output,Serial.used);
  return 0;
}
