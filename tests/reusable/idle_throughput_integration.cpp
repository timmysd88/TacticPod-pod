#include "ble_field_integration.cpp"
#include "../../src/ble_high_speed_window.h"
static_assert(BlePerformance::NotificationQueueCapacity==8,"Keep reviewed HVN queue");
static_assert(BleHighSpeed::MaxWindowSize==16,"Keep reviewed extended window");
extern "C" unsigned combinedPowerThroughputCase(unsigned c){
  const unsigned result=bleFieldCase(c==1?18:6);
  if(result)return result;
  if(!BleRuntime::active() || !fieldBle.entrySafe || !suspended ||
     !reusableReceiver.sleeping || !MonotonicClock::suspended() ||
     GpsReceiver::recordingReady() || reusableReceiver.wakes ||
     Serial1.baud || Tcb3Session::recording() || SPI.programs || SPI.erases)return 200;
  if(c==2){
    // The production BLE branch must keep its 1 ms download pace and the
    // existing 10 ms connected-idle pace without servicing/waking GPS.
    const unsigned writes=Serial1.writeCalls;
    fieldDownloadPace=true;uint32_t at=millis();loop();
    if(uint32_t(millis()-at)!=1)return 201;
    fieldDownloadPace=false;at=millis();loop();
    if(uint32_t(millis()-at)!=10 || Serial1.writeCalls!=writes ||
       !suspended || !reusableReceiver.sleeping || reusableReceiver.wakes)return 202;
  }
  return 0;
}
