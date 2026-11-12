#pragma once
#include <stdint.h>
#include <string.h>

namespace BleDownloadProtocol {
constexpr uint8_t Version=2;
constexpr unsigned HeaderSize=13,Overhead=15,MaxFrameSize=244;

inline uint16_t read16(const uint8_t* p) { return uint16_t(p[0])|uint16_t(p[1])<<8; }
inline uint32_t read32(const uint8_t* p) { return uint32_t(read16(p))|uint32_t(read16(p+2))<<16; }
inline void write16(uint8_t* p,uint16_t v) { p[0]=uint8_t(v);p[1]=uint8_t(v>>8); }
inline void write32(uint8_t* p,uint32_t v) { write16(p,uint16_t(v));write16(p+2,uint16_t(v>>16)); }
inline void write64(uint8_t* p,uint64_t v) { write32(p,uint32_t(v));write32(p+4,uint32_t(v>>32)); }

inline uint16_t crc16(const uint8_t* p,unsigned length) {
  uint16_t crc=0xffff;
  while(length--){
    crc^=uint16_t(*p++)<<8;
    for(unsigned bit=0;bit<8;++bit)crc=(crc&0x8000)?uint16_t((crc<<1)^0x1021):uint16_t(crc<<1);
  }
  return crc;
}

inline unsigned encodeFrame(uint8_t* output,unsigned capacity,uint8_t type,uint16_t transferId,
                            uint32_t sequence,uint16_t offset,const uint8_t* message,
                            uint16_t totalLength,uint16_t& payloadLength,uint16_t& checksum) {
  if(!output || !message || offset>=totalLength)return 0;
  if(capacity>MaxFrameSize)capacity=MaxFrameSize;
  if(capacity<Overhead)return 0;
  const unsigned remaining=totalLength-offset;
  const unsigned payload=remaining<capacity-Overhead?remaining:capacity-Overhead;
  output[0]=Version;output[1]=type;write16(output+2,transferId);
  write32(output+4,sequence);write16(output+8,offset);
  write16(output+10,totalLength);output[12]=uint8_t(payload);
  memcpy(output+HeaderSize,message+offset,payload);
  checksum=crc16(output,HeaderSize+payload);
  write16(output+HeaderSize+payload,checksum);
  payloadLength=uint16_t(payload);
  return Overhead+payload;
}
}
