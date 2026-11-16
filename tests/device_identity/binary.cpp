#include "ficr.h"
#include "../reusable/reset_integration.cpp"
extern "C" unsigned identityBinary(unsigned mode){
  bootDevice();captureUsb=true;
  // Queue identity against backpressure before the binary command takes ownership.
  Serial.freeSpace=0;SerialConsole::handleCommand("DEVICE INFO");SerialConsole::serviceDeviceInfo();
  Serial.freeSpace=256;SerialConsole::handleCommand("TCB READ");
  if(!dumping)return 1;
  for(unsigned n=0;n<100000 && dumping;++n){
    if(mode){SerialConsole::handleCommand("DEVICE INFO");SerialConsole::serviceDeviceInfo();}
    cycle();
  }
  return dumping || Serial.overfilledWrites || SPI.programs || SPI.erases?2:0;
}
