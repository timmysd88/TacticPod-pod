#include "../../src/ble_download_protocol.h"
#include <string.h>

#define CHECK(condition) do { if(!(condition))return __LINE__; } while(0)

int main() {
  const uint8_t check[]="123456789";
  CHECK(BleDownloadProtocol::crc16(check,9)==0x29b1);

  uint8_t message[280],frame[BleDownloadProtocol::MaxFrameSize];
  for(unsigned i=0;i<sizeof(message);++i)message[i]=uint8_t(i*37U);
  const unsigned capacities[]={20,BleDownloadProtocol::MaxFrameSize};
  const unsigned expectedFrames[]={56,2};
  for(unsigned test=0;test<2;++test){
    uint16_t offset=0;unsigned count=0;
    while(offset<sizeof(message)){
      uint16_t payload=0,checksum=0;
      const unsigned length=BleDownloadProtocol::encodeFrame(
        frame,capacities[test],0x84,0x1234,0x89abcdef,offset,message,sizeof(message),payload,checksum);
      CHECK(length && length<=capacities[test]);
      CHECK(frame[0]==BleDownloadProtocol::Version && frame[1]==0x84);
      CHECK(BleDownloadProtocol::read16(frame+2)==0x1234);
      CHECK(BleDownloadProtocol::read32(frame+4)==0x89abcdef);
      CHECK(BleDownloadProtocol::read16(frame+8)==offset);
      CHECK(BleDownloadProtocol::read16(frame+10)==sizeof(message));
      CHECK(frame[12]==payload && payload>0);
      CHECK(memcmp(frame+BleDownloadProtocol::HeaderSize,message+offset,payload)==0);
      CHECK(checksum==BleDownloadProtocol::crc16(frame,length-2));
      CHECK(BleDownloadProtocol::read16(frame+length-2)==checksum);
      offset+=payload;++count;
    }
    CHECK(count==expectedFrames[test]);
  }
  uint16_t payload=0,checksum=0;
  CHECK(BleDownloadProtocol::encodeFrame(frame,14,1,1,0,0,message,sizeof(message),payload,checksum)==0);
  CHECK(BleDownloadProtocol::encodeFrame(frame,20,1,1,0,sizeof(message),message,sizeof(message),payload,checksum)==0);
  return 0;
}
