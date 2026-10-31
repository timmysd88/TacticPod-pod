#define TACTICPOD_REUSABLE 1
#include <stdint.h>
#include "../../variants/tacticpod/Seeed_XIAO_nRF52840/variant.cpp"
static unsigned latch[33],direction[33],trace[64],count,highDrives,chargeChanges;
static void check(unsigned p){
  if(p==14 && direction[p]==OUTPUT && latch[p]==HIGH)++highDrives;
  if(p==22 && direction[p]!=INPUT)++chargeChanges;
}
void digitalWrite(uint32_t p,uint32_t v){
  latch[p]=v;if(count<64)trace[count++]=0x10000|(p<<8)|v;check(p);
}
void pinMode(uint32_t p,uint32_t v){
  direction[p]=v;if(count<64)trace[count++]=0x20000|(p<<8)|v;check(p);
}
extern "C" unsigned startupCase(unsigned c){
  if(g_ADigitalPinMap[14]!=14 || g_ADigitalPinMap[22]!=13 || g_ADigitalPinMap[32]!=31)return 1;
  // Model core init(): OUTSET all ones, direction inherited from earlier boot.
  // USB-only, cell-only and charging do not change this GPIO sequence.
  for(unsigned i=0;i<33;++i){latch[i]=HIGH;direction[i]=INPUT;}
  count=highDrives=chargeChanges=0;
  if(c==3){direction[14]=OUTPUT;check(14);} // Adverse bootloader inheritance.
  if(c==4){pinMode(14,OUTPUT);digitalWrite(14,HIGH);} // Old variant negative control.
  const unsigned earlierHigh=highDrives;
  count=0;
  const unsigned cycles=c==5?10:1;
  for(unsigned cycle=0;cycle<cycles;++cycle){
    if(cycle){for(unsigned i=0;i<33;++i){direction[i]=INPUT;latch[i]=HIGH;}count=0;}
    initVariant();
    if(trace[0]!=(0x10000|(14<<8)|LOW) || trace[1]!=(0x20000|(14<<8)|OUTPUT))return 2;
    if(highDrives!=earlierHigh || chargeChanges || direction[22]!=INPUT)return 3;
    if(latch[14]!=LOW || direction[14]!=OUTPUT)return 4;
    // Preserve original QSPI chip-select and LED state, and leave ADC untouched.
    if(direction[25]!=OUTPUT || latch[25]!=HIGH || direction[32]!=INPUT)return 5;
    for(unsigned p=11;p<=13;++p)if(direction[p]!=OUTPUT || latch[p]!=HIGH)return 6;
  }
  if((c==3 || c==4) && !earlierHigh)return 7;
  return 0;
}
