#include "battery_runtime.h"
#if TACTICPOD_REUSABLE
#include <Arduino.h>
#include <string.h>
#include "monotonic_clock.h"
#include "battery_calibration_store.h"
namespace {
namespace BatteryDetail {
Battery::Monitor policy;
bool initialized=false;uint32_t timeoutCount=0;
constexpr uint8_t chargingStatusPin=23; // Arduino D23 maps to nRF P0.17 (~CHG).
BatteryCalibrationStore::LoadResult calibrationLoad=BatteryCalibrationStore::LoadResult::Empty;
bool persistentCalibration=false;
uint64_t due=0,began=0;unsigned phase=0;
#ifdef TACTICPOD_BATTERY_SIM
uint32_t simMv=4000;bool simValid=false;
#else
alignas(4) volatile int16_t sample=0;
bool offsetDone=false;
static_assert(VBAT_ENABLE==14 && PIN_VBAT==32 && PIN_CHARGING_CURRENT==22,"BSP battery mapping must be reviewed");
void releaseAdc(){NRF_SAADC->ENABLE=0;phase=0;}
#endif
bool decimal(const char*& p,uint32_t& value){
  value=0;unsigned digits=0;while(*p>='0' && *p<='9'){
    if(value>1000000)return false;value=value*10+(*p++-'0');++digits;
  }return digits!=0;
}
}
}
namespace BatteryRuntime {
using namespace BatteryDetail;
Battery::Monitor& monitor(){return policy;}
uint32_t timeouts(){return timeoutCount;}
bool usbPresent(){
#ifdef TACTICPOD_BATTERY_SIM
  return false;
#else
  return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk)!=0;
#endif
}
void begin(){
  if(initialized)return;initialized=true;
  BatteryCalibrationStore::Settings settings{};
  calibrationLoad=BatteryCalibrationStore::load(settings);
  if(calibrationLoad==BatteryCalibrationStore::LoadResult::Valid &&
     !policy.calibrate(settings.gainPpm,settings.offsetMv)) {
    calibrationLoad=BatteryCalibrationStore::LoadResult::Invalid;
  } else {
    persistentCalibration=calibrationLoad==BatteryCalibrationStore::LoadResult::Valid;
  }
#ifndef TACTICPOD_BATTERY_SIM
  // Assert sink before changing direction, leave enabled even during charging.
  digitalWrite(VBAT_ENABLE,LOW);pinMode(VBAT_ENABLE,OUTPUT);
  pinMode(PIN_CHARGING_CURRENT,INPUT); // No pulls: nominal low charge-current mode.
  pinMode(chargingStatusPin,INPUT); // No pulls: observe the charger's open-drain indicator.
#endif
}
#ifdef TACTICPOD_BATTERY_SIM
void inject(uint32_t mv,bool valid){simMv=mv;simValid=valid;}
#endif
void update(bool allow){
  begin();uint64_t now=MonotonicClock::now();
#ifdef TACTICPOD_BATTERY_SIM
  if(now<due)return;due=now+1000000;
  if(simValid)policy.feed(simMv,now);else policy.invalidate();
#else
  // Poll one state per loop. No analogRead(), busy wait, IRQ or TIMER4 use.
  if(phase && now-began>20000){
    NRF_SAADC->TASKS_STOP=1;releaseAdc();++timeoutCount;policy.invalidate();due=now+1000000;return;
  }
  if(!phase){
    if(!allow || now<due)return;
    due=now+1000000;
    if(NRF_SAADC->ENABLE || NRF_SAADC->INTENSET){policy.invalidate();return;} // Other ADC owner: do not reconfigure.
    for(unsigned i=0;i<8;++i){NRF_SAADC->CH[i].PSELP=0;NRF_SAADC->CH[i].PSELN=0;}
    NRF_SAADC->RESOLUTION=SAADC_RESOLUTION_VAL_12bit;
    NRF_SAADC->OVERSAMPLE=0;NRF_SAADC->SAMPLERATE=0;
    NRF_SAADC->CH[0].CONFIG=(SAADC_CH_CONFIG_GAIN_Gain1_6<<SAADC_CH_CONFIG_GAIN_Pos)|
      (SAADC_CH_CONFIG_REFSEL_Internal<<SAADC_CH_CONFIG_REFSEL_Pos)|
      (SAADC_CH_CONFIG_TACQ_40us<<SAADC_CH_CONFIG_TACQ_Pos);
    NRF_SAADC->CH[0].PSELP=SAADC_CH_PSELP_PSELP_AnalogInput7;
    NRF_SAADC->RESULT.PTR=uint32_t(&sample);NRF_SAADC->RESULT.MAXCNT=1;
    NRF_SAADC->EVENTS_STARTED=NRF_SAADC->EVENTS_END=NRF_SAADC->EVENTS_STOPPED=0;
    NRF_SAADC->EVENTS_CALIBRATEDONE=0;NRF_SAADC->ENABLE=1;began=now;
    if(!offsetDone){NRF_SAADC->TASKS_CALIBRATEOFFSET=1;phase=1;}
    else {NRF_SAADC->TASKS_START=1;phase=2;}return;
  }
  if(phase==1 && NRF_SAADC->EVENTS_CALIBRATEDONE){
    offsetDone=true;NRF_SAADC->EVENTS_STOPPED=0;NRF_SAADC->TASKS_STOP=1;phase=5;return;
  }
  if(phase==5 && NRF_SAADC->EVENTS_STOPPED){
    NRF_SAADC->EVENTS_STOPPED=NRF_SAADC->EVENTS_STARTED=NRF_SAADC->EVENTS_END=0;
    NRF_SAADC->TASKS_START=1;phase=2;return;
  }
  if(phase==2 && NRF_SAADC->EVENTS_STARTED){NRF_SAADC->TASKS_SAMPLE=1;phase=3;return;}
  if(phase==3 && NRF_SAADC->EVENTS_END){NRF_SAADC->TASKS_STOP=1;phase=4;return;}
  if(phase==4 && NRF_SAADC->EVENTS_STOPPED){
    uint32_t mv=NRF_SAADC->RESULT.AMOUNT==1?Battery::nominalMillivolts(sample):0;releaseAdc();
    if(mv)policy.feed(mv,now);else policy.invalidate();
  }
#endif
}
bool command(const char* line){
  if(!strcmp(line,"BAT MODE USB")){policy.cell(false);return true;}
  if(!strcmp(line,"BAT MODE CELL")){policy.cell(true);return true;}
  if(!strncmp(line,"BAT CAL SAVE ",13)){
    const char* p=line+13;uint32_t referenceMv=0;
    if(!decimal(p,referenceMv) || *p || referenceMv<2500 || referenceMv>4300 ||
       !policy.fresh(MonotonicClock::now()) || !policy.s.nominalMv)return false;
#ifndef TACTICPOD_BATTERY_SIM
    if(usbPresent() && digitalRead(chargingStatusPin)==LOW)return false;
#endif
    const uint64_t scaled=(uint64_t(referenceMv)*1000000+policy.s.nominalMv/2)/policy.s.nominalMv;
    if(scaled>1200000 || scaled<800000)return false;
    BatteryCalibrationStore::Settings settings{};
    settings.gainPpm=uint32_t(scaled);
    if(!BatteryCalibrationStore::save(settings))return false;
    if(!policy.calibrate(settings.gainPpm,0))return false;
    calibrationLoad=BatteryCalibrationStore::LoadResult::Valid;
    persistentCalibration=true;
    return true;
  }
  if(!strncmp(line,"BAT CAL ",8)){
    const char* p=line+8;uint32_t mv=0;
    if(!decimal(p,mv) || *p || mv<2500 || mv>4300 || !policy.fresh(MonotonicClock::now()) || !policy.s.nominalMv)return false;
    const bool ok=policy.calibrate(uint32_t((uint64_t(mv)*1000000+policy.s.nominalMv/2)/policy.s.nominalMv));
    if(ok)persistentCalibration=false;
    return ok;
  }
  if(!strncmp(line,"BAT LIMITS ",11)){
    const char* p=line+11;uint32_t a[4]{};
    for(unsigned i=0;i<4;++i){if(!decimal(p,a[i]) || a[i]>4300 || (i<3 && *p++!=' '))return false;}
    if(*p)return false;Battery::Limits l;l.warn=a[0];l.stop=a[1];l.hard=a[2];l.recover=a[3];return policy.configure(l);
  }
  return false;
}
unsigned format(char* dst,unsigned capacity){
  unsigned used=0;auto text=[&](const char* p){while(*p && used+1<capacity)dst[used++]=*p++;};
  auto num=[&](uint64_t n){char b[21];unsigned i=20;b[i]=0;do{b[--i]='0'+n%10;n/=10;}while(n);text(b+i);};
  const auto& s=policy.s;text("BAT source=");
#ifdef TACTICPOD_BATTERY_SIM
  text("SIM");
#else
  text("ADC_UNVALIDATED");
#endif
  text(" cell=");num(s.cell);text(" calibrated=");num(s.calibrated);text(" valid=");num(policy.fresh(MonotonicClock::now()));
  text(" trusted=");num(policy.trusted(MonotonicClock::now()));text(" nominal_mV=");num(s.nominalMv);
  text(" raw_mV=");num(s.nominalMv);text(" voltage_mV=");num(s.mv);
  text(" filtered_mV=");num(s.filteredMv);text(" warning=");num(s.warning);text(" stop=");num(s.stop);text(" hard=");num(s.hard);
  text(" adc_timeout=");num(timeoutCount);text(" invalid=");num(s.invalid);text(" usb_vbus=");num(usbPresent());
  text(" charge_status=");
#ifdef TACTICPOD_BATTERY_SIM
  text("UNKNOWN");
#else
  if(!usbPresent())text("UNKNOWN");
  else text(digitalRead(chargingStatusPin)==LOW?"CHARGING":"NOT_CHARGING_OR_FULL");
#endif
  text(" calibration_state=");
  if(calibrationLoad==BatteryCalibrationStore::LoadResult::Invalid)text("INVALID");
  else text(s.calibrated?"CALIBRATED":"UNCALIBRATED");
  text(" calibration_crc_valid=");num(persistentCalibration);
  text(" protection_active=");num(policy.trusted(MonotonicClock::now()));
  text(" thresholds_provisional=1 protocol=2");
  text(" sample_age_ms=");
  const uint64_t now=MonotonicClock::now();
  if(s.valid && now>=s.lastUs)num((now-s.lastUs)/1000);else text("N/A");
  text(" limits=");num(policy.limits.warn);text("/");num(policy.limits.stop);text("/");num(policy.limits.hard);text("/");num(policy.limits.recover);
  text(" gain_ppm=");num(policy.gainPpm());text(" offset_mV=");num(policy.offsetMv());
  text(" adc_errors=");num(s.invalid);
  text("\n");if(capacity)dst[used]=0;return used;
}
}
#endif
