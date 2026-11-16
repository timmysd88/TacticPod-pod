#include "../reusable/console.cpp"
extern "C" unsigned identityUnavailable(unsigned){
  Serial.connected=false;setup();Serial.connected=true;FlashWriteTest::update();
  Serial.used=0;Serial.output[0]=0;
  SerialConsole::handleCommand("DEVICE INFO");SerialConsole::serviceDeviceInfo();
  return strcmp(Serial.output,"\n{\"type\":\"device_info\",\"version\":1,\"status\":\"unavailable\"}\n")?1:0;
}
