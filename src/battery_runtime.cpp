#include "battery_runtime.h"
#if TACTICPOD_REUSABLE
#include <Arduino.h>
#include <string.h>
#include "monotonic_clock.h"
namespace {
namespace BatteryDetail {
Battery::Monitor policy;
Battery::Gauge gauge;
bool loadActive=false;
bool initialized=false;uint32_t timeoutCount=0;
uint64_t due=0,began=0;unsigned phase=0;
#ifdef TACTICPOD_BATTERY_SIM
uint32_t simMv=4000;bool simValid=false;
#else
alignas(4) volatile int16_t sample=0;
bool offsetDone=false;
// One policy measurement = kBurstSamples back-to-back conversions, trimmed-mean filtered.
// A burst whose middle readings still disagree is discarded rather than guessed at.
constexpr uint32_t kMaxMiddleSpreadMv=80,kBurstWindowUs=500000,kMeasureIntervalUs=1000000;
uint32_t burst[BatteryRuntime::kBurstSamples]{};unsigned burstCount=0;
uint64_t burstDue=0,burstStarted=0;uint32_t noisyCount=0;
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
void noteLoad(bool active){loadActive=active;}
bool usbPresent(){
#ifdef TACTICPOD_BATTERY_SIM
  return false;
#else
  return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk)!=0;
#endif
}
void begin(){
  if(initialized)return;initialized=true;
#ifndef TACTICPOD_BATTERY_SIM
  // Assert sink before changing direction, leave enabled even during charging.
  digitalWrite(VBAT_ENABLE,LOW);pinMode(VBAT_ENABLE,OUTPUT);
  pinMode(PIN_CHARGING_CURRENT,INPUT); // No pulls: nominal low charge-current mode.
#endif
}
#ifdef TACTICPOD_BATTERY_SIM
void inject(uint32_t mv,bool valid){simMv=mv;simValid=valid;}
#endif
static void step(bool allow){
  begin();uint64_t now=MonotonicClock::now();
#ifdef TACTICPOD_BATTERY_SIM
  if(now<due)return;due=now+1000000;
  if(simValid)policy.feed(simMv,now);else policy.invalidate();
#else
  // Poll one state per loop. No analogRead(), busy wait, IRQ or TIMER4 use.
  if(phase && now-began>20000){
    NRF_SAADC->TASKS_STOP=1;releaseAdc();++timeoutCount;burstCount=0;policy.invalidate();due=now+kMeasureIntervalUs;return;
  }
  if(!phase){
    if(burstCount && now-burstStarted>kBurstWindowUs)burstCount=0; // Stalled burst: start over.
    if(!allow || now<due)return;
    if(!burstCount){burstStarted=now;burstDue=now+kMeasureIntervalUs;}
    due=burstDue;
    if(NRF_SAADC->ENABLE || NRF_SAADC->INTENSET){burstCount=0;policy.invalidate();return;} // Other ADC owner: do not reconfigure.
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
    if(!mv){burstCount=0;policy.invalidate();return;}
    burst[burstCount++]=mv;
    if(burstCount<kBurstSamples){due=now;return;}
    burstCount=0;
    const uint32_t filtered=Battery::trimmedMean(burst,kBurstSamples);
    if(burst[kBurstSamples-2]-burst[1]>kMaxMiddleSpreadMv){++noisyCount;return;}
    policy.feed(filtered,now);
  }
#endif
}
void update(bool allow){
  step(allow);
  const uint64_t now=MonotonicClock::now();
  gauge.update(policy.trusted(now),policy.s.filteredMv,now,loadActive,usbPresent());
}
bool command(const char* line){
  if(!strcmp(line,"BAT MODE USB")){policy.cell(false);gauge.reset();return true;}
  if(!strcmp(line,"BAT MODE CELL")){policy.cell(true);gauge.reset();return true;}
  if(!strncmp(line,"BAT CAL ",8)){
    const char* p=line+8;uint32_t mv=0;
    if(!decimal(p,mv) || *p || mv<2500 || mv>4300 || !policy.fresh(MonotonicClock::now()) || !policy.s.nominalMv)return false;
    if(!policy.calibrate(uint32_t(uint64_t(mv)*1000000/policy.s.nominalMv)))return false;
    gauge.reset();return true;
  }
  if(!strncmp(line,"BAT LIMITS ",11)){
    const char* p=line+11;uint32_t a[4]{};
    for(unsigned i=0;i<4;++i){if(!decimal(p,a[i]) || a[i]>4300 || (i<3 && *p++!=' '))return false;}
    if(*p)return false;Battery::Limits l;l.warn=a[0];l.stop=a[1];l.hard=a[2];l.recover=a[3];return policy.configure(l);
  }
  return false;
}
void capture(Report& r){
  const uint64_t now=MonotonicClock::now();
  r.fresh=policy.fresh(now);r.trusted=policy.trusted(now);r.calibrated=policy.s.calibrated;r.cell=policy.s.cell;
  r.usb=usbPresent();r.mv=policy.s.mv;r.filteredMv=policy.s.filteredMv;
  r.estimate=gauge.available(r.trusted);r.percent=gauge.percent();
  r.category=gauge.category(r.trusted);r.chargeInfluenced=r.estimate && gauge.chargeInfluencedEstimate();
}
// Human-readable report, one short line per call (USB CDC TX FIFO is only 256 bytes).
// Voltages are shown with 10 mV resolution; that is display rounding, not an accuracy claim.
unsigned formatLine(const Report& r,unsigned index,char* dst,unsigned capacity){
  unsigned used=0;auto text=[&](const char* p){while(*p && used+1<capacity)dst[used++]=*p++;};
  auto volts=[&](bool available,uint32_t mv){
    if(!available){text("n/a");return;}
    const uint32_t c=(mv+5)/10;char b[8]={char('0'+c/100%10),'.',char('0'+c/10%10),char('0'+c%10),' ','V',0};text(b);
  };
  switch(index){
    case 0:text("BAT STATUS");break;
    case 1:text("Voltage: ");volts(r.fresh,r.mv);text(" (raw, calibrated burst reading)");break;
    case 2:text("Filtered: ");volts(r.fresh,r.filteredMv);break;
    case 3:text("Estimated charge: ");
      if(r.estimate){
        char b[5];unsigned n=0;if(r.percent>=100)b[n++]='1';if(r.percent>=10)b[n++]=char('0'+r.percent/10%10);
        b[n++]=char('0'+r.percent%10);b[n++]='%';b[n]=0;text(b);
        text(" (voltage-based estimate)");if(r.chargeInfluenced)text(" USB present: charge-influenced, capped/held");}
      else text("UNKNOWN");
      break;
    case 4:text("Category: ");text(Battery::categoryName(r.category));break;
    case 5:text("ADC: ");text(r.calibrated?"CALIBRATED (operator gain; source unvalidated)":"UNCALIBRATED");break;
    case 6:text("Measurement: ");text(r.trusted?"TRUSTED (software policy)":"UNTRUSTED");break;
    case 7:text("Charging: UNKNOWN (no charge-status signal available)");break;
    case 8:text("Low-voltage protection: ");text(r.trusted?"ARMED":"NOT ARMED");break;
    default:return 0;
  }
  if(used+1<capacity)dst[used++]='\n';
  if(capacity)dst[used]=0;return used;
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
  text(" filtered_mV=");num(s.filteredMv);text(" warning=");num(s.warning);text(" stop=");num(s.stop);text(" hard=");num(s.hard);
  text(" adc_timeout=");num(timeoutCount);
#ifndef TACTICPOD_BATTERY_SIM
  text(" adc_noisy=");num(noisyCount);
#endif
text(" invalid=");num(s.invalid);text(" usb_vbus=");num(usbPresent());
  text(" limits=");num(policy.limits.warn);text("/");num(policy.limits.stop);text("/");num(policy.limits.hard);text("/");num(policy.limits.recover);
  text(" gain_ppm=");num(policy.gainPpm());text("\n");if(capacity)dst[used]=0;return used;
}
}
#endif
