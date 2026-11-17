#include "../../src/gps_power.h"
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
struct Io {
  GpsPower::Controller power;
  uint32_t clock=0,host=38400,receiver=38400;
  unsigned mode=0,sets=0,edges=0,calls=0,bad=0,n=0;
  uint8_t queued[24]{};
  bool stuck=false;
  bool txReady(){return !stuck;}
  void baud(uint32_t value){host=value;}
  size_t write(const uint8_t* p,size_t size){
    ++calls;if(size>24){++bad;return 0;}
    memcpy(queued,p,size);n=size;
    if(mode==7)stuck=true;
    return mode==6?size-1:size;
  }
  void service(){
    if(!n)return;
    unsigned size=n;n=0;uint8_t* p=queued;
    if(size==1){++edges;if(p[0]!=0xff)++bad;return;}
    uint8_t a=0,b=0;for(unsigned i=2;i<size-2;++i){a+=p[i];b+=a;}
    if(p[0]!=0xb5 || p[1]!=0x62 || p[4]!=size-8 || p[5] || a!=p[size-2] || b!=p[size-1]){++bad;return;}
    if(host!=receiver || mode==2)return;
    if(p[2]==10 && p[3]==4 && size==8){
      uint8_t version[70]{};memcpy(version+40,mode==3?"FWVER=SPG 5.20":"FWVER=SPG 5.10",14);
      power.onFrame(10,4,version,70,clock);return;
    }
    if(p[2]==6 && p[3]==0x8a && size==20){
      if(p[6] || p[7]!=1 || p[8] || p[9] || GpsPower::read32(p+10)!=0x40520001 || GpsPower::read32(p+14)!=38400)++bad;
      ++sets;
      if(mode==5){const uint8_t nak[]={6,0x8a};power.onFrame(5,0,nak,2,clock);return;}
      receiver=38400;return; // No ACK: must verify actual baud after transition.
    }
    if(p[2]==6 && p[3]==0x8b && size==16){
      if(p[6] || p[7] || p[8] || p[9] || GpsPower::read32(p+10)!=0x40520001)++bad;
      if(mode==8)return;
      uint8_t reply[12]={1,0,0,0,1,0,0x52,0x40};
      uint32_t value=mode==4?9600:38400;
      for(unsigned i=0;i<4;++i)reply[8+i]=value>>(8*i);
      if(mode==9){reply[1]=1;power.onFrame(6,0x8b,reply,12,clock);reply[1]=0;reply[4]^=1;power.onFrame(6,0x8b,reply,12,clock);return;}
      power.onFrame(6,0x8b,reply,12,clock);return;
    }
    ++bad;
  }
  void run(){for(unsigned i=0;i<12000 && power.active();++i){power.update(*this,clock++);service();}power.restoreFailureUart(*this);}
};
extern "C" unsigned powerCase(unsigned c){
  if(c==10){
    uint8_t p[24];GpsPower::sleepPacket(p);
    const uint8_t expected[]={0xb5,0x62,2,0x41,16,0,0,0,0,0,0,0,0,0,6,0,0,0,8,0,0,0,0x61,0x6b};
    CHECK(!memcmp(p,expected,24));return 0;
  }
  Io io;io.mode=c;if(c==1 || c==5)io.receiver=9600;
  if(c==11)io.clock=0xffffff00U;
  if(c==12){
    io.power.asleep();CHECK(!io.power.ready() && !io.power.active());
    for(unsigned i=0;i<4;++i){io.power.beginWake(io.clock);io.run();CHECK(io.power.ready());io.power.asleep();}
    CHECK(io.edges==4 && !io.bad);return 0;
  }
  io.power.beginWake(io.clock);io.run();
  CHECK(!io.bad);
  if(c==0 || c==1 || c==11){CHECK(io.power.ready() && io.host==38400);CHECK(io.sets==(c==1?1u:0u));}
  else {CHECK(io.power.failed() && !io.power.ready());if(c!=7)CHECK(io.host==38400);}
  CHECK(io.calls<16);return 0;
}
