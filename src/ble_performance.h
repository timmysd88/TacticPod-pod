#pragma once
#include <Arduino.h>
#include "ble_download_protocol.h"

namespace BlePerformance {
constexpr unsigned SnapshotSize=176;
constexpr uint16_t SnapshotVersion=1;

struct Counters {
  uint16_t mtu=0,lastPayload=0;
  bool connected=false,listPending=false,haveListBegin=false,haveConnectionList=false;
  bool cacheStepActive=false,saturated=false;
  uint32_t connectedAt=0,cacheStartedAt=0,cacheElapsedMs=0,cacheSteps=0,cacheState=0;
  uint32_t listRequests=0,listRequestedUs=0,firstListRequestedMs=0;
  uint32_t listToBeginUs=0,firstListToBeginMs=0,connectionToListMs=0;
  uint32_t fragments=0,acks=0,retransmissions=0,notifyFailures=0,ackTimeouts=0,duplicateAcks=0;
  uint32_t ackSamples=0,retransmittedAcks=0,ackMinUs=0,ackMaxUs=0;
  uint32_t notifyMaxUs=0,readCalls=0,readMaxUs=0;
  uint64_t ackTotalUs=0,notifyTotalUs=0,readBytes=0,readTotalUs=0;
  uint64_t ackedPayloadBytes=0,notificationBytes=0,cacheServiceUs=0,cacheReadUs=0;
};

inline Counters& counters(){static Counters value;return value;}
inline void add(uint32_t& value,uint32_t amount){
  if(amount>UINT32_MAX-value){value=UINT32_MAX;counters().saturated=true;}
  else value+=amount;
}
inline void add(uint64_t& value,uint64_t amount){
  if(amount>~uint64_t(0)-value){value=~uint64_t(0);counters().saturated=true;}
  else value+=amount;
}
inline void observeConnection(uint16_t mtu){
  auto& s=counters();
  if(!s.connected){s.connected=true;s.connectedAt=millis();}
  s.mtu=mtu;
}
inline void invalidateCache(){
  auto& s=counters();
  s.cacheState=s.cacheStartedAt=s.cacheElapsedMs=s.cacheSteps=0;
  s.cacheServiceUs=s.cacheReadUs=0;
}
inline void startCache(){
  auto& s=counters();s.cacheStartedAt=millis();s.cacheState=1;
}
inline void finishCache(bool failed){
  auto& s=counters();
  if(s.cacheState==1)s.cacheElapsedMs=uint32_t(millis()-s.cacheStartedAt);
  s.cacheState=failed?3:2;
}
struct CacheStep {
  uint32_t started;
  CacheStep():started(micros()){counters().cacheStepActive=true;}
  ~CacheStep(){
    auto& s=counters();add(s.cacheSteps,1);add(s.cacheServiceUs,uint64_t(uint32_t(micros()-started)));
    s.cacheStepActive=false;
  }
};
struct FlashRead {
  bool enabled,cache;
  unsigned bytes;
  uint32_t started;
  FlashRead(bool active,unsigned length):enabled(active),cache(counters().cacheStepActive),
    bytes(length),started(active?micros():0){}
  ~FlashRead(){
    if(!enabled)return;
    const uint32_t elapsed=uint32_t(micros()-started);
    auto& s=counters();add(s.readCalls,1);add(s.readBytes,uint64_t(bytes));
    add(s.readTotalUs,uint64_t(elapsed));if(elapsed>s.readMaxUs)s.readMaxUs=elapsed;
    if(cache)add(s.cacheReadUs,uint64_t(elapsed));
  }
};
inline void listRequested(uint32_t receivedUs,uint32_t receivedMs){
  auto& s=counters();add(s.listRequests,1);s.listRequestedUs=receivedUs;
  if(!s.listPending){s.listPending=true;s.firstListRequestedMs=receivedMs;}
}
inline void listBegin(){
  auto& s=counters();
  s.listToBeginUs=uint32_t(micros()-s.listRequestedUs);
  s.firstListToBeginMs=uint32_t(millis()-s.firstListRequestedMs);
  s.listPending=false;s.haveListBegin=true;
  if(!s.haveConnectionList){
    s.connectionToListMs=uint32_t(millis()-s.connectedAt);s.haveConnectionList=true;
  }
}
inline void notification(bool accepted,bool retransmission,unsigned bytes,unsigned payload,uint32_t elapsedUs){
  auto& s=counters();add(s.notifyTotalUs,uint64_t(elapsedUs));
  if(elapsedUs>s.notifyMaxUs)s.notifyMaxUs=elapsedUs;
  if(!accepted){add(s.notifyFailures,1);return;}
  add(s.fragments,1);add(s.notificationBytes,uint64_t(bytes));s.lastPayload=uint16_t(payload);
  if(retransmission)add(s.retransmissions,1);
}
inline void acknowledged(unsigned payload,bool retransmitted,uint32_t elapsedUs){
  auto& s=counters();add(s.acks,1);add(s.ackedPayloadBytes,uint64_t(payload));
  // An ACK after retransmission cannot identify which transmission it acknowledged.
  if(retransmitted){add(s.retransmittedAcks,1);return;}
  if(!s.ackSamples || elapsedUs<s.ackMinUs)s.ackMinUs=elapsedUs;
  if(elapsedUs>s.ackMaxUs)s.ackMaxUs=elapsedUs;
  add(s.ackSamples,1);add(s.ackTotalUs,uint64_t(elapsedUs));
}
inline void snapshot(uint8_t* bytes){
  using namespace BleDownloadProtocol;
  const auto& s=counters();const uint32_t now=millis();
  memset(bytes,0,SnapshotSize);memcpy(bytes,"TPD1",4);
  write16(bytes+4,SnapshotVersion);write16(bytes+6,SnapshotSize);
  write32(bytes+8,(s.connected?1U:0U)|(s.cacheState==1?2U:0U)|
    (s.cacheState==2?4U:0U)|(s.cacheState==3?8U:0U)|(s.haveListBegin?16U:0U));
  unsigned capacity=s.mtu>3?s.mtu-3:0;
  if(capacity>MaxFrameSize)capacity=MaxFrameSize;
  write16(bytes+12,s.mtu);write16(bytes+14,uint16_t(capacity));
  write16(bytes+16,uint16_t(capacity>Overhead?capacity-Overhead:0));
  write16(bytes+18,s.lastPayload);
  write32(bytes+20,s.connected?uint32_t(now-s.connectedAt):0);
  write32(bytes+24,s.cacheState==1?uint32_t(now-s.cacheStartedAt):s.cacheElapsedMs);
  write32(bytes+28,s.cacheSteps);write32(bytes+32,s.cacheState);
  write32(bytes+36,s.listRequests);write32(bytes+40,s.listToBeginUs);
  write32(bytes+44,s.firstListToBeginMs);write32(bytes+48,s.fragments);
  write32(bytes+52,s.acks);write32(bytes+56,s.retransmissions);
  write32(bytes+60,s.notifyFailures);write32(bytes+64,s.ackTimeouts);
  write32(bytes+68,s.duplicateAcks);write32(bytes+72,s.ackSamples);
  write32(bytes+76,s.retransmittedAcks);write64(bytes+80,s.ackTotalUs);
  write32(bytes+88,s.ackMinUs);write32(bytes+92,s.ackMaxUs);
  write64(bytes+96,s.notifyTotalUs);write32(bytes+104,s.notifyMaxUs);
  write32(bytes+108,s.readCalls);write64(bytes+112,s.readBytes);
  write64(bytes+120,s.readTotalUs);write32(bytes+128,s.readMaxUs);
  write64(bytes+132,s.ackedPayloadBytes);write64(bytes+140,s.notificationBytes);
  write32(bytes+148,s.connectionToListMs);write64(bytes+152,s.cacheServiceUs);
  write64(bytes+160,s.cacheReadUs);write32(bytes+168,now);
  write32(bytes+172,s.saturated?1U:0U);
}
}
