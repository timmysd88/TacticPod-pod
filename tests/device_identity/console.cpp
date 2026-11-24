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
    bool* flags[]={&TcbDetail::pending,&TcbDetail::locked,&TcbDetail::resetting,&Recorder::mockBusy,&GpsReceiver::mockScanActive};
    for(auto flag:flags){*flag=true;VERIFY(!identitySilent());clearIdentityOutput();*flag=false;}
    Recorder::mockBusy=true;Recorder::mockFault=true;
    VERIFY(!identitySilent());clearIdentityOutput();Recorder::mockBusy=Recorder::mockFault=false;
    state=State::Ready;VERIFY(!identitySilent());clearIdentityOutput();state=State::Idle;
    TcbDetail::owned=true;VERIFY(identitySilent());TcbDetail::owned=false;
    TcbDetail::dumping=true;VERIFY(identitySilent());TcbDetail::dumping=false;
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
  }else if(mode==8){
    TcbDetail::locked=true;Recorder::mockBusy=true;Recorder::mockFault=true;
    Recorder::mockDiagnosticStage="Fault";
    VERIFY(SerialConsole::handleCommand("FACTORY DIAG"));
    for(unsigned n=0;n<30;++n)updateFactory();
    VERIFY(!factoryOutputActive && strstr(Serial.output,"FACTORY JEDEC PASS raw=0B4018"));
    VERIFY(strstr(Serial.output,"FACTORY BATTERY") && strstr(Serial.output,"protection=NOT_ARMED"));
    VERIFY(strstr(Serial.output,"FACTORY RECORDER FAIL stage=Fault"));
    VERIFY(strstr(Serial.output,"END_FACTORY_DIAG"));
  }
  VERIFY(SPI.programs==writes && SPI.erases==erases && SPI.enables==enables);
  return 0;
}
