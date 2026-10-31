#include "Arduino.h"
uint32_t mockTime=0;
unsigned mockRedWrites=0, mockImuAxisReads=0;
MockSerial Serial;
#include "../../src/monotonic_clock.h"
uint64_t compactTestNow=0;
namespace MonotonicClock { void begin() {} uint64_t now() {return compactTestNow;} }
namespace Recorder { bool exporting() {return false;} }
#include "../../src/compact_recorder.cpp"

uint8_t compactTestInput[8192]={};
uint8_t compactTestOutput[8228]={};
uint32_t compactTestResult[16]={};

extern "C" unsigned prepare(unsigned size) {
  CompactRecorder::stop();CompactRecorder::clear();
  if(!CompactRecorder::start()) return 1;
  unsigned at=0;
  while(at<size) {
    unsigned length=compactTestInput[at+2] | unsigned(compactTestInput[at+3])<<8;
    if(!length || length>size-at) return 2;
    CompactRecorder::observe(compactTestInput+at,length);at+=length;
  }
  CompactRecorder::stop();
  return CompactRecorder::stats().rejected ? 3 : 0;
}
extern "C" unsigned slice(unsigned allow) {
  CompactRecorder::update(allow!=0);
  size_t size=0;const uint8_t* data=CompactRecorder::output(size);
  if(data) {memcpy(compactTestOutput,data,size);compactTestResult[0]=size;
    auto s=CompactRecorder::stats();compactTestResult[2]=s.maxSliceUs;
    compactTestResult[3]=s.maxBlockWorkUs;compactTestResult[4]=s.maxUnits;return 1;}
  return 0;
}
extern "C" unsigned run(unsigned unused) {
  (void)unused;
  for(unsigned n=0;n<30000;++n) if(slice(1)) {compactTestResult[1]=n+1;return 0;}
  return 4;
}
extern "C" unsigned runScenario(unsigned scenario) {
  using namespace CompactRecorder;
  static uint8_t imu[24]={1,1,24,0};
  auto putTime=[](uint64_t n) {for(unsigned i=0;i<8;++i) imu[4+i]=n>>(8*i);};
  if(scenario==0) { // No output sink: bounded explicit fault, accepted prefix retained.
    start();
    for(unsigned n=0;n<1000;++n) {
      compactTestNow=uint64_t(n)*10000;putTime(compactTestNow);observe(imu,24);
      for(unsigned t=0;t<20;++t) update();
    }
    auto s=stats();return !s.capturing && s.fault && s.rejected==1 && s.outputReady==2 &&
      s.completed==2 && s.inputUsed==2 && s.backpressureSlices>0 ? 0 : 10;
  }
  if(scenario==1) {
    start();for(unsigned n=0;n<700;++n) {putTime(uint64_t(n)*10000);observe(imu,24);}
    auto s=stats();return s.fault && s.accepted==400 && s.rejected==1 ? 0 : 11;
  }
  if(scenario==2) {
    start();putTime(1);observe(imu,24);stop();
    for(unsigned n=0;n<1000;++n) update(false);
    return stats().completed==0 && stats().inputUsed==1 ? 0 : 12;
  }
  if(scenario==3) {
    start();putTime(1);observe(imu,24);stop();run(0);
    size_t n;const uint8_t* a=output(n);unsigned length=n;uint8_t first=a?a[0]:0;
    for(unsigned i=0;i<1000;++i) update();
    const uint8_t* b=output(n);
    bool kept=a && a==b && length==n && b[0]==first && memcmp(b,compactTestOutput,length)==0;
    release();return kept && !output(n) && stats().consumed==1 ? 0 : 13;
  }
  if(scenario==4) {
    start();if(clear()) return 14;putTime(1);observe(imu,24);stop();
    if(start()) return 15;
    if(!clear() || !start()) return 16;
    return stats().accepted==0 && !stats().fault ? 0 : 17;
  }
  if(scenario==5) {start();putTime(10);observe(imu,24);putTime(9);observe(imu,24);return stats().fault && stats().accepted==1 ? 0:18;}
  if(scenario==6) {start();imu[1]=2;observe(imu,24);return stats().fault && !stats().accepted ? 0:19;}
  if(scenario==7) {start();putTime(UINT64_MAX);observe(imu,24);return stats().fault && !stats().accepted ? 0:20;}
  if(scenario==8) {
    start();putTime(0);observe(imu,24);compactTestNow=2000000;update();
    return stats().sealed==1 && stats().accepted==1 ? 0:21;
  }
  if(scenario==9) {
    Serial.connected=false;handleCommand("COMP START");
    for(unsigned i=0;i<20;++i) {putTime(i);observe(imu,24);update();}
    return stats().accepted==20 && !Serial.overfilledWrites ? 0:22;
  }
  return 99;
}
