#pragma once
#include "ble_download_protocol.h"

namespace BleHighSpeed {
using namespace BleDownloadProtocol;

constexpr uint8_t Version=3;
constexpr unsigned WindowSize=4,HeaderSize=9,Overhead=11,AckSize=12;

class SendWindow {
 public:
  enum class Result : uint8_t {
    Ok, Full, Empty, Wait, Invalid, Stale, RetryExhausted, QueueExhausted
  };
  struct Frame {
    uint8_t bytes[MaxFrameSize]{};
    uint32_t sequence=0,lastSentAt=0,lastAttemptAt=0;
    uint16_t length=0;
    uint8_t attempts=0,queueFailures=0;
    bool submitted=false,received=false;
  };

  explicit SendWindow(uint16_t id):transferId_(id) {}

  void reset(uint16_t id) {
    for(unsigned i=0;i<WindowSize;++i)frames_[i]=Frame{};
    base_=next_=0;transferId_=id;finalQueued_=false;
  }

  Result append(const uint8_t* payload,unsigned length,unsigned capacity,bool final) {
    if(!transferId_ || !payload || !length || finalQueued_ ||
       capacity>MaxFrameSize || capacity<Overhead+1 ||
       length>capacity-Overhead || next_==UINT32_MAX)return Result::Invalid;
    if(next_-base_==WindowSize)return Result::Full;
    Frame& frame=frames_[next_%WindowSize];
    frame=Frame{};
    frame.sequence=next_;
    frame.length=uint16_t(Overhead+length);
    frame.bytes[0]=Version;frame.bytes[1]=final?1:0;
    write16(frame.bytes+2,transferId_);
    write32(frame.bytes+4,next_);
    frame.bytes[8]=uint8_t(length);
    memcpy(frame.bytes+HeaderSize,payload,length);
    write16(frame.bytes+HeaderSize+length,crc16(frame.bytes,HeaderSize+length));
    ++next_;
    finalQueued_=final;
    return Result::Ok;
  }

  Result candidate(uint32_t now,const Frame*& output) const {
    output=nullptr;
    if(base_==next_)return Result::Empty;
    for(uint32_t sequence=base_;sequence<next_;++sequence){
      const Frame& frame=frames_[sequence%WindowSize];
      if(frame.received)continue;
      if(frame.queueFailures && uint32_t(now-frame.lastAttemptAt)<100)continue;
      if(frame.submitted && uint32_t(now-frame.lastSentAt)<1000)continue;
      if(frame.attempts>=5)return Result::RetryExhausted;
      if(frame.queueFailures>=5)return Result::QueueExhausted;
      output=&frame;
      return Result::Ok;
    }
    return Result::Wait;
  }

  Result submitted(uint32_t sequence,uint32_t now,bool accepted) {
    const Frame* selected=nullptr;
    if(candidate(now,selected)!=Result::Ok || selected->sequence!=sequence)return Result::Invalid;
    Frame& frame=frames_[sequence%WindowSize];
    frame.lastAttemptAt=now;
    if(accepted){
      frame.submitted=true;frame.lastSentAt=now;
      ++frame.attempts;frame.queueFailures=0;
    }else{
      ++frame.queueFailures;
    }
    return Result::Ok;
  }

  Result acknowledge(const uint8_t* bytes,unsigned length) {
    if(!bytes || length!=AckSize || bytes[0]!=Version || bytes[1]!=1 ||
       read16(bytes+2)!=transferId_ || read16(bytes+10)!=crc16(bytes,10))
      return Result::Invalid;
    const uint32_t next=read32(bytes+4);
    const uint16_t bitmap=read16(bytes+8);
    if((bitmap&1U) || (bitmap&~((1U<<WindowSize)-1U)))return Result::Invalid;
    if(next<base_)return Result::Stale;
    if(next>next_)return Result::Invalid;
    for(uint32_t sequence=base_;sequence<next;++sequence)
      if(!frames_[sequence%WindowSize].submitted)return Result::Invalid;
    for(unsigned bit=1;bit<WindowSize;++bit){
      if(!(bitmap&(1U<<bit)))continue;
      if(uint64_t(next)+bit>=next_ || !frames_[(next+bit)%WindowSize].submitted)
        return Result::Invalid;
    }
    for(unsigned bit=1;bit<WindowSize;++bit)
      if(bitmap&(1U<<bit))frames_[(next+bit)%WindowSize].received=true;
    base_=next;
    return Result::Ok;
  }

  uint32_t base() const {return base_;}
  uint32_t next() const {return next_;}
  bool finalQueued() const {return finalQueued_;}

 private:
  Frame frames_[WindowSize]{};
  uint32_t base_=0,next_=0;
  uint16_t transferId_;
  bool finalQueued_=false;
};
}
