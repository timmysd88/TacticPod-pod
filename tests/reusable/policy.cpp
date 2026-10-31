#include "../../src/battery_monitor.h"
using namespace Battery;
extern "C" unsigned policyCase(unsigned c){
  Monitor m;uint64_t t=1000000;
  auto feed=[&](unsigned mv,unsigned n){while(n--){m.feed(mv,t);t+=1000000;}};
  if(c==0){feed(2500,30);return m.s.stop || m.s.warning || m.trusted(t)?1:0;}
  if(c==1){m.cell(true);feed(2500,30);return m.s.stop || m.s.warning || m.mayStart(t)?2:0;}
  if(c==2){m.calibrate(1000000);feed(2500,30);return m.s.stop || m.s.warning?3:0;}
  m.calibrate(1000000);m.cell(true);feed(4000,5);
  if(!m.trusted(t) || !m.mayStart(t))return 4;
  if(c==3){
    feed(3550,20);if(!m.s.warning || m.s.stop)return 5;
    feed(3650,20);if(!m.s.warning)return 6;
    feed(4000,20);return m.s.warning || m.s.stop?7:0;
  }
  if(c==4){feed(3400,20);if(!m.s.stop || m.s.hard || m.mayStart(t))return 8;
    feed(4000,20);return m.s.stop || !m.mayStart(t)?9:0;}
  if(c==5){feed(3200,1);if(m.s.stop)return 10;feed(4000,1);feed(3200,2);return !m.s.stop || !m.s.hard?11:0;}
  if(c==6){m.invalidate();if(m.trusted(t))return 12;feed(0,30);return m.s.stop || m.s.warning?13:0;}
  if(c==7){if(m.trusted(t+5000000) || m.mayStart(t+5000000))return 14;
    m.feed(3200,t+10000000);return m.s.stop?15:0;}
  if(c==8){Limits x;x.hard=3600;x.stop=3500;if(m.configure(x))return 16;
    if(m.calibrate(799999) || m.calibrate(1200001) || m.calibrate(1000000,201))return 17;
    return nominalMillivolts(1500)!=3904 || nominalMillivolts(0) || nominalMillivolts(4095)?18:0;}
  if(c==9){m.feed(3000,1);return m.s.stop || m.trusted(1)?19:0;}
  if(c==10){feed(3200,1);feed(4000,5);return m.s.warning || m.s.stop?20:0;}
  if(c==11){feed(4000,5);m.cell(false);feed(2500,20);return m.s.stop || m.s.warning || !m.mayStart(t)?21:0;}
  if(c==12){uint64_t last=m.s.lastUs;m.feed(3200,last);m.feed(3200,last);return m.s.stop || m.s.valid?22:0;}
  if(c==13){m.cell(true);feed(3200,3);return m.mayStart(t)?23:0;}
  if(c==14){m.cell(true);feed(4000,1);feed(2000,1);feed(4000,12);return m.s.stop || m.s.warning?24:0;}
  if(c==15){feed(3400,1);return m.mayStart(t) || m.s.stop?25:0;}
  return 99;
}
