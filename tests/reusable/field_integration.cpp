// Production setup/loop, physical-button path, asynchronous recorder and UBX
// transaction model. Never notify the LED directly in these integration cases.
#include "integration.cpp"
uint32_t fieldStep=0;

unsigned eventCount(FieldTrace::Kind kind) {
  const auto& b=FieldTrace::buffer();unsigned n=0;
  for(unsigned i=0;i<b.count;++i)if(b.events[(b.head+i)%64].kind==kind)++n;
  return n;
}
void runMs(unsigned ms) {const uint32_t at=millis();while(uint32_t(millis()-at)<ms)cycle();}
bool green(){return mockPinLevels[LED_GREEN]==LOW && mockPinLevels[LED_RED]!=LOW;}
extern "C" unsigned fieldCase(unsigned c) {
  using namespace TcbDetail;
  if(c==6)mockButtonLow=true;
  if(c==9)mockImuBeginStatus=1;
  Serial.connected=true;setup();startupPending=false;
  if(c<8)waitIdle(); // Startup LED cases observe the boot window itself.
  if(c==8 || c==9) {
    // Startup indication: exactly three 150 ms blue flashes, only after a successful startup; never green/red.
    unsigned flashes=0;bool was=false,bad=false;uint32_t edgeAt=millis();const uint32_t t0=millis();
    while(uint32_t(millis()-t0)<2500) {
      cycle();
      const bool blue=mockPinLevels[LED_BLUE]==LOW;
      if(mockPinLevels[LED_GREEN]==LOW || mockPinLevels[LED_RED]==LOW)bad=true;
      if(blue!=was) {
        const uint32_t now=millis(),held=now-edgeAt;
        if(blue){++flashes;if(flashes>1 && (held<148 || held>152))bad=true;}
        else if(held<148 || held>152)bad=true;
        edgeAt=now;was=blue;
      }
    }
    if(bad || was)return c==8?90:92;
    return flashes==(c==8?3u:0u)?0:(c==8?91:93);
  }
  fieldStep=1;
  // Fixtures preserve the same old physical session prefix used by recorder tests.
  BatteryRuntime::inject(4000,true);mockImuReadCostUs=30;SPI.programDelayMs=10;
  runMs(600);
  if(c==7) {
    auto& b=FieldTrace::buffer();b=FieldTrace::Buffer{};
    for(unsigned i=0;i<80;++i)FieldTrace::note(FieldTrace::Kind::Press,millis(),0,i);
    if(b.count!=64 || b.overwritten!=16)return 70;
    Serial.freeSpace=0;SerialConsole::handleCommand("FIELD EVENTS");runMs(100);
    if(b.count!=64 || Serial.overfilledWrites)return 71;
    Serial.freeSpace=256;runMs(100);
    return b.count || b.remaining || !strstr(Serial.output,"overwritten=16") ? 72:0;
  }
  if(c==6){runMs(6000);return owned || pending || eventCount(FieldTrace::Kind::StartAccepted)?60:0;}
  if(c==4 || c==5) {
    if(c==4)reusableReceiver.scenario=107; // Commit NAK, rollback verified: target NOT ready.
    else {reusableGpsEnabled=false;receivedValid=false;}
    mockButtonLow=false;runMs(50);mockButtonLow=true;runMs(3100);
    for(unsigned n=0;n<1000000 && pending;++n)cycle();
    if(owned || pending || SPI.programs || eventCount(FieldTrace::Kind::SessionCreated))return 41;
    if(!GpsReceiver::preparationFailed() || GpsProfile::currentVerified())return 42;
    return 0;
  }
  if(c==2) { // A receiver reset during standby is discovered/recovered on wake.
    memcpy(reusableReceiver.db,ProfileModel::desired,sizeof(reusableReceiver.db));
    if(!GpsProfile::startCheck(false))return 20;
    while(GpsProfile::active()){cycle();}
    if(!GpsProfile::currentVerified())return 21;
    Serial.connected=false;runMs(3500);
    if(!standbyPolicy.inStandby() || mockPinLevels[LED_BLUE]!=HIGH)return 22;
    memcpy(reusableReceiver.db,ProfileModel::initial,sizeof(reusableReceiver.db));
    if(GpsProfile::currentVerified())return 23;
  }
  if(c==3) { // Audit detects profile loss while awake, ACK alone never suffices.
    memcpy(reusableReceiver.db,ProfileModel::desired,sizeof(reusableReceiver.db));
    if(!GpsProfile::startCheck(false))return 30;
    while(GpsProfile::active())cycle();
    if(!GpsProfile::currentVerified())return 31;
    memcpy(reusableReceiver.db,ProfileModel::initial,sizeof(reusableReceiver.db));
    if(!GpsProfile::startCheck(true))return 32;
    unsigned unverified=0;
    while(GpsProfile::active()){if(!GpsProfile::currentVerified())++unverified;cycle();}
    if(!unverified || !GpsProfile::currentVerified() || !reusableReceiver.testConfigured())return 33;
    if(!eventCount(FieldTrace::Kind::GpsInvalidated))return 34;
    return reusableReceiver.safe()?0:35;
  }
  // Idle USB must never emit periodic recording colours.
  if(c!=2){runMs(3000);if(mockPinLevels[LED_BLUE]!=HIGH || green())return 1;}
  // Bounce shorter than debounce cannot create a start.
  mockButtonLow=true;runMs(10);mockButtonLow=false;runMs(50);
  if(owned || pending || SPI.programs)return 2;
  mockButtonLow=true;runMs(3100);
  if(!pending && !owned)return 3;
  if(pending && SPI.programs)return 4; // No flash allocation before fresh GPS audit.
  for(unsigned n=0;n<600000 && !owned && !locked;++n)cycle();
  fieldStep=2;
  if(!owned)return 50;
  if(locked)return 51;
  if(!reusableReceiver.testConfigured())return 52;
  if(!GpsProfile::currentVerified())return 53;
  // Holding the same press past startup never creates a second session.
  runMs(3500);
  if(!green() || mockPinLevels[LED_BLUE]!=HIGH || mockPinLevels[LED_RED]!=HIGH)return 80; // Solid green while recording.
  fieldStep=3;
  if(eventCount(FieldTrace::Kind::StartAccepted)!=1 || eventCount(FieldTrace::Kind::SessionCreated)!=1)return 6;
  mockButtonLow=false;runMs(80);
  // A standalone long hold while recording must not stop or start another session.
  mockButtonLow=true;runMs(3200);mockButtonLow=false;runMs(80);
  if(!Tcb3Session::recording() || eventCount(FieldTrace::Kind::SessionCreated)!=1)return 7;
  fieldStep=4;
  mockButtonLow=true;runMs(200);mockButtonLow=false;runMs(100);
  if(eventCount(FieldTrace::Kind::StopArmed)!=1)return 8;
  fieldStep=5;
  mockButtonLow=true;
  for(unsigned n=0;n<60000 && !eventCount(FieldTrace::Kind::StopAccepted);++n)cycle();
  if(!eventCount(FieldTrace::Kind::StopAccepted) || Tcb3Session::recording() || !finishing)return 9;
  if(green())return 10; // Solid green ends the instant recording stops, before the footer commits.
  fieldStep=6;
  if(c==1)SPI.denyWel=true;
  mockButtonLow=false;
  for(unsigned n=0;n<500000 && owned && !locked;++n) {
    if(!store.stats.finished && green())return 11;
    cycle();
  }
  if(c==1) {
    bool redSeen=false;const uint32_t faultAt=millis();
    while(uint32_t(millis()-faultAt)<2100) {
      cycle();if(mockPinLevels[LED_RED]==LOW && mockPinLevels[LED_GREEN]==HIGH)redSeen=true;
    }
    if(!Tcb3Session::faulted() || eventCount(FieldTrace::Kind::Complete) || green())return 12;
    return redSeen?0:43;
  }
  fieldStep=7;
  if(owned || locked || !store.stats.finished || reason!=Tcb3::Reason::Clean)return 13;
  cycle();
  if(green() || eventCount(FieldTrace::Kind::Complete)!=1)return 14; // Clean finish adds no stop animation.
  runMs(700);if(green() || mockPinLevels[LED_BLUE]!=HIGH || mockPinLevels[LED_RED]!=HIGH)return 17;
  if(eventCount(FieldTrace::Kind::Complete)!=1 || eventCount(FieldTrace::Kind::SessionCreated)!=1)return 18;
  // Return to standby with USB removed, then wake on USB without any new start.
  Serial.connected=false;runMs(3500);
  if(!standbyPolicy.inStandby())return 19;
  Serial.connected=true;runMs(100);
  return owned || pending || eventCount(FieldTrace::Kind::SessionCreated)!=1 ? 40 : 0;
}
