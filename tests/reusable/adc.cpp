#define TACTICPOD_REUSABLE 1
#define TACTICPOD_TCB3_FLASH_ENABLED 1
#include "hw/Arduino.h"
uint8_t writes[33]{},modes[33]{};
Adc adc;Power power;uint64_t clockUs=1000000;
namespace MonotonicClock {uint64_t now(){return clockUs;}}
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
// One policy measurement is a burst of conversions; each samples the next raw value in `raws`.
void measure(const int* raws,unsigned n){
  complete(raws[0]);
  for(unsigned i=1;i<n;++i){BatteryRuntime::update(true);complete(raws[i]);}
}
void measureSteady(int raw){int r[BatteryRuntime::kBurstSamples];for(auto& x:r)x=raw;measure(r,BatteryRuntime::kBurstSamples);}
bool has(const char* line,const char* text){return strstr(line,text)!=nullptr;}
extern "C" unsigned adcCase(unsigned c){
  BatteryRuntime::begin();
  if(writes[14]!=LOW || modes[14]!=OUTPUT || modes[22]!=INPUT)return 1;
  BatteryRuntime::update(false);if(adc.ENABLE)return 2;
  if(c==2 || c==3){
    adc.ENABLE=c==2?1:0;adc.INTENSET=c==3?1:0;BatteryRuntime::update(true);
    return adc.TASKS_START || adc.TASKS_CALIBRATEOFFSET || (c==2 && !adc.ENABLE)?3:0;
  }
  BatteryRuntime::update(true);
  if(!adc.TASKS_CALIBRATEOFFSET || adc.CH[0].PSELP!=SAADC_CH_PSELP_PSELP_AnalogInput7 || adc.RESULT.PTR%4)return 4;
  if(c==1){clockUs+=20001;BatteryRuntime::update(false);return adc.ENABLE || BatteryRuntime::timeouts()!=1 || BatteryRuntime::monitor().s.valid?5:0;}
  if(c==4)complete(0);
  else if(c==7){
    // A single wild conversion in the burst is trimmed away; the result stays near 3.9 V.
    const int raws[5]={1500,1502,3000,1498,1500};measure(raws,5);
  }else if(c==8){
    // Disagreeing middle readings: the whole burst is discarded, never averaged into a guess.
    const int raws[5]={1400,1500,1600,1700,1800};measure(raws,5);
  }else measureSteady(1500);
  if(adc.ENABLE)return 6;
  auto& m=BatteryRuntime::monitor();
  if(c==4)return m.s.valid || m.s.stop || !m.s.invalid?7:0;
  if(c==8)return m.s.valid || m.s.samples || m.s.invalid || BatteryDetail::noisyCount!=1?15:0;
  if(c==7)return !m.s.valid || m.s.samples!=1 || m.s.nominalMv<3900 || m.s.nominalMv>3910?16:0;
  if(!m.s.valid || m.s.nominalMv!=3904 || m.s.calibrated || m.s.cell || m.s.stop)return 8;
  if(m.s.samples!=1)return 17; // Five conversions make exactly one policy sample.
  if(c==5){power.USBREGSTATUS=1;return !BatteryRuntime::usbPresent()?9:0;}
  if(c==6){
    if(BatteryRuntime::command("BAT CAL 0") || BatteryRuntime::command("BAT LIMITS 3500 3600 3300 3700"))return 10;
    if(!BatteryRuntime::command("BAT CAL 3904") || !BatteryRuntime::command("BAT MODE CELL"))return 11;
    if(m.trusted(clockUs))return 12;
    for(unsigned i=0;i<3;++i){clockUs+=1000000;BatteryRuntime::update(true);measureSteady(1500);}
    if(!m.trusted(clockUs) || m.s.stop)return 13;
    char output[256];unsigned n=BatteryRuntime::format(output,sizeof(output));
    BatteryRuntime::Report r;char l[160];
    BatteryRuntime::capture(r);
    if(!r.estimate || r.percent!=64 || r.category!=Battery::Category::Half || !r.trusted)return 18;
    const char* want[]={"Battery status","Voltage: 3.90 V","Filtered: 3.90 V","Estimated charge: 64% (voltage-based estimate)",
      "Category: HALF","ADC: CALIBRATED","Measurement: TRUSTED","Charging: UNKNOWN","Low-voltage protection: ARMED"};
    for(unsigned i=0;i<9;++i){unsigned n=BatteryRuntime::formatLine(r,i,l,sizeof(l));
      if(!n || n>200 || !has(l,want[i]) || l[n-1]!='\n')return 19;}
    if(BatteryRuntime::formatLine(r,9,l,sizeof(l)))return 20;
    if(!strstr(output,"gain_ppm=1000000") || !strstr(output,"limits=3600/3500/3300/3700") || n>=255 || output[n-1]!='\n')return 14;
  }
  if(c==9){
    // Untrusted: never a category or percentage, only the plain measurements and honest flags.
    BatteryRuntime::Report r;char l[160];BatteryRuntime::capture(r);
    if(r.estimate || r.trusted || r.category!=Battery::Category::Unknown)return 21;
    const char* want[]={"Battery status","Voltage: 3.90 V","Filtered: 3.90 V","Estimated charge: UNKNOWN",
      "Category: UNKNOWN","ADC: UNCALIBRATED","Measurement: UNTRUSTED","Charging: UNKNOWN","Low-voltage protection: NOT ARMED"};
    for(unsigned i=0;i<9;++i){BatteryRuntime::formatLine(r,i,l,sizeof(l));if(!has(l,want[i]))return 22;}
    char output[300];BatteryRuntime::format(output,sizeof(output));
    if(!has(output,"source=ADC_UNVALIDATED") || !has(output,"trusted=0") || !has(output,"calibrated=0") || !has(output,"cell=0"))return 23;
    // Armed and calibrated, then a stale reading: category falls back to UNKNOWN.
    if(!BatteryRuntime::command("BAT CAL 3904") || !BatteryRuntime::command("BAT MODE CELL"))return 24;
    for(unsigned i=0;i<3;++i){clockUs+=1000000;BatteryRuntime::update(true);measureSteady(1500);}
    BatteryRuntime::capture(r);if(!r.estimate || r.category!=Battery::Category::Half)return 25;
    clockUs+=3500000;BatteryRuntime::update(false);BatteryRuntime::capture(r);
    if(r.trusted || r.estimate || r.category!=Battery::Category::Unknown)return 26;
  }
  return 0;
}
