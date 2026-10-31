#pragma once
#include <stdint.h>
namespace Battery {
struct Limits { uint16_t warn=3600,stop=3500,hard=3300,recover=3700; };
struct State {
  uint32_t samples=0,invalid=0,nominalMv=0,mv=0,filteredMv=0;
  uint64_t lastUs=0;
  bool valid=false,calibrated=false,cell=false,warning=false,stop=false,hard=false;
};
// Portable, fixed-memory policy. Hardware presence is NEVER inferred from ADC.
class Monitor {
 public:
  State s{}; Limits limits{};
  uint32_t gainPpm() const {return gain;}
  bool configure(Limits p){
    if(p.hard<2500 || p.hard>=p.stop || p.stop>=p.warn || p.warn>=p.recover || p.recover>4300)return false;
    limits=p;resetPolicy();return true;
  }
  void cell(bool value){s.cell=value;resetPolicy();}
  bool calibrate(uint32_t gainPpm,int32_t offsetMv=0){
    if(gainPpm<800000 || gainPpm>1200000 || offsetMv < -200 || offsetMv>200)return false;
    gain=gainPpm;offset=offsetMv;s.calibrated=true;resetPolicy();return true;
  }
  void invalidate(){s.valid=false;++s.invalid;lowSince=0;hardCount=recoveryCount=0;}
  bool fresh(uint64_t now) const{return s.valid && now>=s.lastUs && now-s.lastUs<=3000000;}
  bool trusted(uint64_t now) const{return s.cell && s.calibrated && fresh(now) && warm>=3;}
  bool mayStart(uint64_t now) const{
    return !s.cell || (trusted(now) && !s.stop && s.filteredMv>limits.stop && s.mv>limits.stop);
  }
  void feed(uint32_t nominalMv,uint64_t now){
    if(nominalMv<2000 || nominalMv>4400 || (s.samples && now<=s.lastUs)){invalidate();return;}
    int64_t adjusted=(int64_t(nominalMv)*gain+500000)/1000000+offset;
    if(adjusted<2000 || adjusted>4400){invalidate();return;}
    if(s.samples && now-s.lastUs>3000000){warm=0;lowSince=0;hardCount=recoveryCount=0;}
    s.lastUs=now;s.nominalMv=nominalMv;s.mv=uint32_t(adjusted);s.valid=true;++s.samples;
    bool initializing=warm<3;
    history[index]=s.mv;index=(index+1)%3;if(warm<3)++warm;
    if(warm<3){s.filteredMv=s.mv;return;}
    uint32_t a=history[0],b=history[1],c=history[2],t;
    if(a>b){t=a;a=b;b=t;}if(b>c){t=b;b=c;c=t;}if(a>b){t=a;a=b;b=t;}
    s.filteredMv=initializing?b:(3*s.filteredMv+b+2)/4;
    if(!trusted(now)){lowSince=0;hardCount=recoveryCount=0;return;}
    if(s.mv<=limits.hard){if(hardCount<2)++hardCount;}else hardCount=0;
    if(hardCount>=2){s.stop=s.hard=s.warning=true;}
    if(s.filteredMv<=limits.warn)s.warning=true;
    if(s.filteredMv<=limits.stop){
      if(!lowSince)lowSince=now;
      if(now-lowSince>=3000000)s.stop=s.warning=true;
    }else lowSince=0;
    if(s.filteredMv>=limits.recover){
      if(++recoveryCount>=3){s.stop=s.hard=s.warning=false;recoveryCount=3;}
    }else recoveryCount=0;
  }
 private:
  uint32_t gain=1000000,history[3]{};int32_t offset=0;
  uint64_t lowSince=0;unsigned index=0,warm=0,hardCount=0,recoveryCount=0;
  void resetPolicy(){s.warning=s.stop=s.hard=s.valid=false;warm=index=hardCount=recoveryCount=0;lowSince=0;}
};
// 12-bit ADC, 0.6V internal reference/gain 1/6, 1M top + 510k bottom divider.
inline uint32_t nominalMillivolts(int32_t raw){
  if(raw<=0 || raw>=4095)return 0;
  return uint32_t((int64_t(raw)*3600*1510+4095*510/2)/(4095*510));
}
}
