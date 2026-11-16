#include "ficr.h"
#include "../reusable/console.cpp"
#define VERIFY(x) do{if(!(x))return __LINE__;}while(0)
static void clearIdentityOutput(){Serial.used=0;Serial.output[0]=0;}
static bool identitySilent(){SerialConsole::handleCommand("DEVICE INFO");SerialConsole::serviceDeviceInfo();return Serial.used==0;}
extern "C" unsigned identityConsole(unsigned mode){
  Serial.connected=false;setup();Serial.connected=true;FlashWriteTest::update();VERIFY(!strstr(Serial.output,"device_info"));clearIdentityOutput();
  const unsigned writes=SPI.programs,erases=SPI.erases,enables=SPI.enables;
  if(mode==0){
    Serial.rx="DEVICE INFO\r\n";FlashWriteTest::update();
    VERIFY(strstr(Serial.output,"\"uid_hex\":\"0e8a89b7289430bd\""));
    VERIFY(strstr(Serial.output,"\"device_id\":\"1047801283069882557\""));
    VERIFY(Serial.writeCalls==1 && !Serial.overfilledWrites);
    VERIFY(!SPI.programs && !SPI.erases && !SPI.enables);
    return 0;
  }
  if(mode==1){
    bool* flags[]={&TcbDetail::pending,&TcbDetail::owned,&TcbDetail::dumping,&TcbDetail::locked,&TcbDetail::resetting,&Recorder::mockBusy,&GpsReceiver::mockScanActive};
    for(auto flag:flags){*flag=true;VERIFY(identitySilent());*flag=false;}
    state=State::Ready;VERIFY(identitySilent());state=State::Idle;
    RamObservations::handleCommand("OBS START");clearIdentityOutput();VERIFY(identitySilent());
    RamObservations::handleCommand("OBS STOP");RamObservations::handleCommand("OBS CLEAR");clearIdentityOutput();
    CompactRecorder::handleCommand("COMP START");clearIdentityOutput();VERIFY(identitySilent());
    CompactRecorder::handleCommand("COMP STOP");CompactRecorder::handleCommand("COMP CLEAR");clearIdentityOutput();
  }else if(mode==2){
    Serial.freeSpace=0;VERIFY(identitySilent());Serial.freeSpace=256;SerialConsole::serviceDeviceInfo();
    VERIFY(Serial.used>0 && Serial.writeCalls==1 && !Serial.overfilledWrites);
    clearIdentityOutput();SerialConsole::serviceDeviceInfo();VERIFY(!Serial.used);
  }else if(mode==3){
    Serial.freeSpace=0;VERIFY(identitySilent());SerialConsole::handleCommand("IMU OFF");clearIdentityOutput();
    Serial.freeSpace=256;SerialConsole::serviceDeviceInfo();VERIFY(!Serial.used);
  }else if(mode==4){
    Serial.freeSpace=0;VERIFY(identitySilent());mockTime+=2000;Serial.freeSpace=256;
    SerialConsole::serviceDeviceInfo();VERIFY(!Serial.used);
  }else if(mode==5){
    Serial.freeSpace=0;VERIFY(identitySilent());Serial.connected=false;SerialConsole::serviceDeviceInfo();
    Serial.connected=true;Serial.freeSpace=256;SerialConsole::serviceDeviceInfo();VERIFY(!Serial.used);
  }else if(mode==6){
    Serial.freeSpace=0;VERIFY(identitySilent());TcbDetail::dumping=true;Serial.freeSpace=256;
    SerialConsole::serviceDeviceInfo();VERIFY(!Serial.used);TcbDetail::dumping=false;
    SerialConsole::serviceDeviceInfo();VERIFY(!Serial.used);
  }else if(mode==7){
    mockTime=0xfffffff0;Serial.freeSpace=0;VERIFY(identitySilent());mockTime+=100;
    Serial.freeSpace=256;SerialConsole::serviceDeviceInfo();VERIFY(Serial.used>0);
  }
  VERIFY(SPI.programs==writes && SPI.erases==erases && SPI.enables==enables);
  return 0;
}
