#include "field_integration.cpp"
void powerReady(){const uint32_t at=millis();do{cycle();}while((!GpsProfile::currentVerified() || GpsReceiver::busy()) && uint32_t(millis()-at)<45000);}
extern "C" unsigned idlePowerCase(unsigned c){
  using namespace TcbDetail;
  Serial.connected=true;setup();
  if(c==8){reusableReceiver.sleeping=true;reusableReceiver.factoryWake=true;}
  powerReady();
  if(!GpsProfile::currentVerified() || !GpsProfile::standbySupported() || owned || pending || SPI.programs)return 1;
  if(c==8)return reusableReceiver.wakes && reusableReceiver.baudChanges && !owned && !pending?0:2;
  if(c==13){
    startupPending=true;receivedValid=false;startedAt=millis();reusableGpsEnabled=false;
    mockUarte.stuckStop=true;
    if(!GpsUartScan::start())return 22;
    const unsigned before=Serial1.writeCalls;runMs(5000);
    return !GpsUartScan::faulted() || GpsPower::controller().active() ||
      GpsPower::controller().failed() || Serial1.writeCalls!=before || owned || pending?23:0;
  }
  if(c==4){
    // Admission, recording, USB export and reset all own flash/GPS resources.
    bool* owners[]={&pending,&owned,&dumping,&resetting};
    for(auto owner:owners){
      *owner=true;
      const bool direct=GpsReceiver::suspend(),entry=enterStandby();
      *owner=false;
      if(direct || entry || suspended || SPI.sleeps || reusableReceiver.sleeps)return 3;
    }
    identityRunning=true;const bool entry=enterStandby();identityRunning=false;
    return entry || SPI.sleeps || suspended?3:0;
  }
  if(c==5){GpsReceiver::handleCommand("GPS RAW");return GpsReceiver::suspend() || suspended?4:0;}
  if(c==6 || c==7){
    if(c==6)Serial1.stuckTx=true;else Serial1.shortTx=true;
    const uint32_t at=millis();
    const bool result=enterStandby();
    if(result || suspended || SPI.deepPower || uint32_t(millis()-at)>25 || !GpsPower::controller().active())return 5;
    return owned || pending || SPI.programs || SPI.erases?6:0;
  }
  if(c==10){Profile::supportedVersion=false;return enterStandby() || suspended || SPI.deepPower?7:0;}
  reusableReceiver.factoryWake=c==1 || c==3;
  Serial.connected=false;runMs(3500);reusableReceiver.service();
  if(!standbyPolicy.inStandby() || !suspended || !SPI.deepPower || !reusableReceiver.sleeping || reusableReceiver.sleeps!=1)return 8;
  if(GpsReceiver::recordingReady() || GpsProfile::currentVerified())return 9;
  if(c==2)reusableReceiver.missing=true;
  if(c==3)reusableReceiver.rejectBaud=true;
  if(c==12){mockUarte.TXD.AMOUNT=1;Serial1.stuckTx=true;}
  Serial.connected=true;runMs(100);
  if(suspended || standbyPolicy.inStandby() || SPI.deepPower || GpsReceiver::recordingReady())return 10;
  if(c==12){
    // A previous one-byte DMA completion must not satisfy a new stuck wake TX.
    const unsigned before=Serial1.writeCalls;
    runMs(11000);
    return !GpsPower::controller().failed() || Serial1.writeCalls!=before ||
      !before || Serial1.writeSizes[before-1]!=2 || owned || pending || SPI.programs?21:0;
  }
  if(c==11){
    const unsigned writes=Serial1.writeCalls;
    GpsReceiver::handleCommand("GPS CONFIG");GpsReceiver::handleCommand("GPS SCAN");
    return GpsConfig::active() || GpsUartScan::active() || Serial1.writeCalls!=writes?11:0;
  }
  // An explicit request is pending; capture/flash writes cannot precede recovery.
  if(!Tcb3Session::startFromButton() || owned || SPI.programs)return 12;
  for(unsigned i=0;i<1000000 && pending;++i){
    if(!GpsProfile::currentVerified() && SPI.programs)return 13;
    cycle();
  }
  if(c==2 || c==3){
    if(owned || pending || SPI.programs || !GpsReceiver::preparationFailed())return 14;
    // A fresh deliberate request retries a transient failure; it never auto-starts.
    reusableReceiver.missing=reusableReceiver.rejectBaud=false;
    runMs(500);if(owned || pending || SPI.programs)return 15;
    if(!Tcb3Session::startFromButton())return 16;
    for(unsigned i=0;i<1000000 && pending;++i)cycle();
  }
  if(!owned || !GpsProfile::currentVerified() || !reusableReceiver.testConfigured() || Serial1.baud!=38400 || reusableReceiver.invalid)return 17;
  Tcb3Session::handleCommand("TCB STOP");
  for(unsigned i=0;i<500000 && owned;++i)cycle();
  if(owned || locked || SPI.erases)return 18;
  if(c==9){
    for(unsigned pass=0;pass<3;++pass){
      // Reuse the bounded mock TX trace once all prior traffic has been consumed.
      runMs(1000);reusableReceiver.handled=Serial1.writeCalls=0;
      Serial.connected=false;runMs(3500);reusableReceiver.service();
      if(!suspended || !SPI.deepPower)return 19;
      Serial.connected=true;powerReady();
      if(suspended || owned || pending || !GpsProfile::currentVerified())return 20;
    }
  }
  return 0;
}
