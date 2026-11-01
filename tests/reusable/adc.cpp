#define TACTICPOD_REUSABLE 1
#define TACTICPOD_BATTERY_STORE_SIM 1
#define TACTICPOD_TCB3_FLASH_ENABLED 1
#include "hw/Arduino.h"
uint8_t writes[33]{},modes[33]{};
Adc adc;Power power;uint64_t clockUs=1000000;
namespace MonotonicClock {uint64_t now(){return clockUs;}}
#include "../../src/battery_calibration_store.cpp"
#include "../../src/battery_runtime.cpp"
void complete(int raw){
  if(adc.TASKS_CALIBRATEOFFSET){
    adc.EVENTS_CALIBRATEDONE=1;BatteryRuntime::update(false);
    adc.EVENTS_STOPPED=1;BatteryRuntime::update(false);adc.TASKS_CALIBRATEOFFSET=0;
  }
  adc.EVENTS_STARTED=1;BatteryRuntime::update(false);
  BatteryDetail::sample=raw;adc.RESULT.AMOUNT=1;adc.EVENTS_END=1;BatteryRuntime::update(false);
  adc.EVENTS_STOPPED=1;BatteryRuntime::update(false);
}
extern "C" unsigned adcCase(unsigned c){
  BatteryCalibrationStore::resetForTest();
  BatteryRuntime::begin();
  if(writes[14]!=LOW || modes[14]!=OUTPUT || modes[22]!=INPUT || modes[23]!=INPUT)return 1;
  BatteryRuntime::update(false);if(adc.ENABLE)return 2;
  if(c==2 || c==3){
    adc.ENABLE=c==2?1:0;adc.INTENSET=c==3?1:0;BatteryRuntime::update(true);
    return adc.TASKS_START || adc.TASKS_CALIBRATEOFFSET || (c==2 && !adc.ENABLE)?3:0;
  }
  BatteryRuntime::update(true);
  if(!adc.TASKS_CALIBRATEOFFSET || adc.CH[0].PSELP!=SAADC_CH_PSELP_PSELP_AnalogInput7 || adc.RESULT.PTR%4)return 4;
  if(c==1){clockUs+=20001;BatteryRuntime::update(false);return adc.ENABLE || BatteryRuntime::timeouts()!=1 || BatteryRuntime::monitor().s.valid?5:0;}
  complete(c==4?0:1500);
  if(adc.ENABLE)return 6;
  auto& m=BatteryRuntime::monitor();
  if(c==4)return m.s.valid || m.s.stop || !m.s.invalid?7:0;
  if(!m.s.valid || m.s.nominalMv!=3904 || m.s.calibrated || m.s.cell || m.s.stop)return 8;
  if(c==5){power.USBREGSTATUS=1;return !BatteryRuntime::usbPresent()?9:0;}
  if(c==6){
    if(BatteryRuntime::command("BAT CAL 0") || BatteryRuntime::command("BAT LIMITS 3500 3600 3300 3700"))return 10;
    if(!BatteryRuntime::command("BAT CAL 3904") || !BatteryRuntime::command("BAT MODE CELL"))return 11;
    if(m.trusted(clockUs))return 12;
    for(unsigned i=0;i<3;++i){clockUs+=1000000;BatteryRuntime::update(true);complete(1500);}
    if(!m.trusted(clockUs) || m.s.stop)return 13;
    char output[512];unsigned n=BatteryRuntime::format(output,sizeof(output));
    if(!strstr(output,"gain_ppm=1000000") || !strstr(output,"limits=3600/3500/3300/3700") || n>=sizeof(output)-1 || output[n-1]!='\n')return 14;
  }
  if(c==7 || c==8){
    if(!BatteryRuntime::command("BAT CAL SAVE 4000"))return 15;
    BatteryCalibrationStore::Settings saved{};
    if(BatteryCalibrationStore::load(saved)!=BatteryCalibrationStore::LoadResult::Valid ||
       saved.gainPpm<1024000 || saved.gainPpm>1025000)return 16;
    if(c==8){
      if(BatteryRuntime::command("BAT CAL SAVE 4301") ||
         BatteryRuntime::command("BAT CAL SAVE 2000"))return 17;
      BatteryCalibrationStore::Settings unchanged{};
      if(BatteryCalibrationStore::load(unchanged)!=BatteryCalibrationStore::LoadResult::Valid ||
         unchanged.gainPpm!=saved.gainPpm || unchanged.sequence!=saved.sequence)return 18;
    }
  }
  if(c==9){
    BatteryCalibrationStore::Settings first{};first.gainPpm=1010000;
    BatteryCalibrationStore::Settings second{};second.gainPpm=1020000;
    if(!BatteryCalibrationStore::save(first) || !BatteryCalibrationStore::save(second))return 19;
    BatteryCalibrationStore::corruptForTest(1,4);
    BatteryCalibrationStore::Settings recovered{};
    if(BatteryCalibrationStore::load(recovered)!=BatteryCalibrationStore::LoadResult::Valid ||
       recovered.gainPpm!=first.gainPpm)return 20;
    BatteryCalibrationStore::corruptForTest(0,4);
    if(BatteryCalibrationStore::load(recovered)!=BatteryCalibrationStore::LoadResult::Invalid)return 21;
  }
  return 0;
}
