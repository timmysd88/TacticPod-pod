#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// MAX-M10S / SPG 5.10 only. See docs/idle-power-phase1.md for protocol evidence.
// Hardware is supplied by Io; no storage, pin numbers or framework changes here.
namespace GpsPower {
constexpr uint32_t kBaud = 38400, kFactoryBaud = 9600;
constexpr uint32_t kBaudKey = 0x40520001;
inline uint32_t read32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
inline unsigned packet(uint8_t* p, uint8_t cls, uint8_t id, unsigned payload) {
  p[0]=0xb5;p[1]=0x62;p[2]=cls;p[3]=id;p[4]=uint8_t(payload);p[5]=0;
  uint8_t a=0,b=0;
  for(unsigned i=2;i<payload+6;++i){a+=p[i];b+=a;}
  p[payload+6]=a;p[payload+7]=b;return payload+8;
}
inline void sleepPacket(uint8_t* p) {
  memset(p,0,24);
  // version=0, reserved=0, duration=0 (indefinite), backup|force, UART RX only.
  p[14]=0x06;p[18]=0x08;packet(p,0x02,0x41,16);
}
inline bool supported(const uint8_t* p,size_t n) {
  const char fw[]="FWVER=SPG 5.10";
  if(n<40 || (n-40)%30)return false;
  for(unsigned i=40;i+30<=n;i+=30)
    if(!memcmp(p+i,fw,sizeof(fw)-1) && (p[i+sizeof(fw)-1]==0 || p[i+sizeof(fw)-1]==' '))return true;
  return false;
}
enum class State : uint8_t { Awake, Asleep, Edge, Settle, Version, VersionWait,
  Baud, BaudDrain, Readback, ReadbackWait, Ready, Failed };
class Controller {
 public:
  State state=State::Awake;
  bool active() const {return state>=State::Edge && state<=State::ReadbackWait;}
  bool failed() const {return state==State::Failed;}
  bool ready() const {return state==State::Ready;}
  const char* error() const {return reason;}
  void asleep(){state=State::Asleep;reason="standby requested; current unmeasured";}
  void beginWake(uint32_t now){state=State::Edge;started=at=now;attempt=0;changed=false;reason="waking";}
  // Frames must already have passed the shared bounded UBX checksum parser.
  void onFrame(uint8_t cls,uint8_t id,const uint8_t* p,size_t n,uint32_t now) {
    if(uint32_t(now-at)>=1500)return;
    if(state==State::VersionWait && cls==10 && id==4){
      if(!supported(p,n)){fail("wake: unsupported MON-VER");return;}
      state=baud()==kBaud?State::Readback:State::Baud;at=now;
    } else if(state==State::ReadbackWait && cls==6 && id==0x8b && n==12 &&
              p[0]==1 && !p[1] && !p[2] && !p[3] && read32(p+4)==kBaudKey){
      if(read32(p+8)!=kBaud){fail("wake: baud readback mismatch");return;}
      state=State::Ready;reason="UART verified; recording profile still required";
    } else if((state==State::ReadbackWait || state==State::BaudDrain) &&
              cls==5 && id==0 && n==2 && p[0]==6 && p[1]==(state==State::ReadbackWait?0x8b:0x8a))
      fail("wake: receiver rejected UART recovery");
  }
  template<class Io> void update(Io& io,uint32_t now) {
    if(!active())return;
    if(uint32_t(now-started)>=10000){finishFailure(io,"wake: operation deadline");return;}
    uint8_t p[24]={};
    switch(state){
      case State::Edge:
        if(!io.txReady()){if(uint32_t(now-at)>=1000)finishFailure(io,"wake: TX incomplete");return;}
        io.baud(baud());
        // The first byte is sacrificial: its start edge wakes UART RX. It is NOT
        // a command and is never used as evidence that the receiver is ready.
        p[0]=0xff;
        if(io.write(p,1)!=1){finishFailure(io,"wake: short wake byte");return;}
        state=State::Settle;at=now;break;
      case State::Settle:
        // 100 ms is host pacing, NOT an assumed receiver readiness guarantee.
        if(!io.txReady()){if(uint32_t(now-at)>=1000)finishFailure(io,"wake: wake TX incomplete");return;}
        if(uint32_t(now-at)>=100){state=State::Version;at=now;}break;
      case State::Version:
        if(!io.txReady())return;
        if(io.write(p,packet(p,10,4,0))!=8){finishFailure(io,"wake: short MON-VER");return;}
        state=State::VersionWait;at=now;break;
      case State::VersionWait:
        if(uint32_t(now-at)>=1500){
          if(!io.txReady()){finishFailure(io,"wake: MON-VER TX incomplete");return;}
          if(changed || ++attempt==4){finishFailure(io,"wake: no supported receiver response");return;}
          state=State::Edge;at=now;
        }break;
      case State::Baud:
        if(!io.txReady())return;
        // VALSET v0, RAM layer ONLY, transactionless, one U4 key. Never BBR/flash.
        p[6]=0;p[7]=1;
        for(unsigned i=0;i<4;++i){p[10+i]=uint8_t(kBaudKey>>(8*i));p[14+i]=uint8_t(kBaud>>(8*i));}
        if(io.write(p,packet(p,6,0x8a,12))!=20){finishFailure(io,"wake: short baud write");return;}
        state=State::BaudDrain;at=now;break;
      case State::BaudDrain:
        if(!io.txReady()){if(uint32_t(now-at)>=1000)finishFailure(io,"wake: baud TX incomplete");return;}
        // An ACK can be lost at the baud transition. Verify at 38400 instead.
        if(uint32_t(now-at)>=100){changed=true;attempt=0;state=State::Edge;at=now;}break;
      case State::Readback:
        if(!io.txReady())return;
        for(unsigned i=0;i<4;++i)p[10+i]=uint8_t(kBaudKey>>(8*i));
        if(io.write(p,packet(p,6,0x8b,8))!=16){finishFailure(io,"wake: short baud poll");return;}
        state=State::ReadbackWait;at=now;break;
      case State::ReadbackWait:
        if(uint32_t(now-at)>=1500)finishFailure(io,"wake: baud readback timeout");
        break;
      default:break;
    }
  }
  template<class Io> void restoreFailureUart(Io& io){if(failed() && io.txReady())io.baud(kBaud);}
 private:
  uint32_t started=0,at=0;unsigned attempt=0;bool changed=false;
  const char* reason="awake";
  uint32_t baud()const{return attempt%2?kFactoryBaud:kBaud;}
  void fail(const char* why){state=State::Failed;reason=why;}
  template<class Io> void finishFailure(Io& io,const char* why){fail(why);restoreFailureUart(io);}
};
inline Controller& controller(){static Controller instance;return instance;}
}
