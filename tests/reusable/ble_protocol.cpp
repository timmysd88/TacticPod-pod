#include "../../src/ble_download_protocol.h"
#include "../../src/ble_high_speed_window.h"
#include <string.h>

#define CHECK(condition) do { if(!(condition))return __LINE__; } while(0)

static void windowAck(uint8_t* ack,uint16_t id,uint32_t next,uint16_t bitmap) {
  ack[0]=3;ack[1]=1;BleDownloadProtocol::write16(ack+2,id);
  BleDownloadProtocol::write32(ack+4,next);BleDownloadProtocol::write16(ack+8,bitmap);
  BleDownloadProtocol::write16(ack+10,BleDownloadProtocol::crc16(ack,10));
}

static int highSpeedWindowCase(unsigned capacity) {
  using BleHighSpeed::SendWindow;
  using Result=SendWindow::Result;
  SendWindow window(7);
  uint8_t payload[233],ack[12];
  for(unsigned i=0;i<sizeof(payload);++i)payload[i]=uint8_t(i);
  const unsigned length=capacity-BleHighSpeed::Overhead;
  for(unsigned i=0;i<4;++i)CHECK(window.append(payload,length,capacity,false)==Result::Ok);
  CHECK(window.append(payload,length,capacity,false)==Result::Full);
  const SendWindow::Frame* frame=nullptr;
  for(unsigned i=0;i<4;++i){
    CHECK(window.candidate(100,frame)==Result::Ok && frame->sequence==i);
    CHECK(frame->length==capacity && frame->bytes[0]==3 && frame->bytes[8]==length);
    CHECK(BleDownloadProtocol::read16(frame->bytes+capacity-2)==
          BleDownloadProtocol::crc16(frame->bytes,capacity-2));
    CHECK(window.submitted(i,100,true)==Result::Ok);
  }
  CHECK(window.candidate(1099,frame)==Result::Wait);
  CHECK(window.submitted(0,1099,true)==Result::Invalid);
  windowAck(ack,8,0,0);
  CHECK(window.acknowledge(ack,sizeof(ack))==Result::Invalid);
  windowAck(ack,7,0,0x000c);
  CHECK(window.acknowledge(ack,sizeof(ack))==Result::Ok);
  CHECK(window.candidate(1100,frame)==Result::Ok && frame->sequence==0);
  uint8_t original[BleDownloadProtocol::MaxFrameSize];
  memcpy(original,frame->bytes,frame->length);
  CHECK(window.submitted(0,1100,true)==Result::Ok);
  CHECK(window.candidate(1100,frame)==Result::Ok && frame->sequence==1);
  CHECK(window.submitted(1,1100,true)==Result::Ok);
  CHECK(window.candidate(2100,frame)==Result::Ok && frame->sequence==0);
  CHECK(memcmp(original,frame->bytes,frame->length)==0);
  windowAck(ack,7,4,0);
  CHECK(window.acknowledge(ack,sizeof(ack))==Result::Ok && window.base()==4);
  windowAck(ack,7,0,0);
  CHECK(window.acknowledge(ack,sizeof(ack))==Result::Stale);
  CHECK(window.append(payload,length,capacity,true)==Result::Ok && window.finalQueued());
  CHECK(window.append(payload,length,capacity,false)==Result::Invalid);
  windowAck(ack,7,5,0);
  CHECK(window.acknowledge(ack,sizeof(ack))==Result::Invalid);
  CHECK(window.candidate(2200,frame)==Result::Ok && frame->sequence==4 && frame->bytes[1]==1);
  CHECK(window.submitted(4,2200,true)==Result::Ok);
  ack[10]^=1;
  CHECK(window.acknowledge(ack,sizeof(ack))==Result::Invalid && window.base()==4);
  windowAck(ack,7,5,0);
  CHECK(window.acknowledge(ack,sizeof(ack))==Result::Ok);
  CHECK(window.candidate(2200,frame)==Result::Empty);
  return 0;
}

static int highSpeedFailureCase() {
  using BleHighSpeed::SendWindow;
  using Result=SendWindow::Result;
  const uint8_t payload[]={1};
  uint8_t ack[12];
  SendWindow queue(1);
  CHECK(queue.append(payload,1,20,false)==Result::Ok);
  const SendWindow::Frame* frame=nullptr;
  for(unsigned i=0;i<5;++i){
    CHECK(queue.candidate(i*100,frame)==Result::Ok);
    CHECK(queue.submitted(0,i*100,false)==Result::Ok);
  }
  CHECK(queue.candidate(500,frame)==Result::QueueExhausted);
  windowAck(ack,1,1,0);
  CHECK(queue.acknowledge(ack,sizeof(ack))==Result::Invalid);
  SendWindow retry(2);
  CHECK(retry.append(payload,1,20,false)==Result::Ok);
  for(unsigned i=0;i<5;++i){
    CHECK(retry.candidate(i*1000,frame)==Result::Ok);
    CHECK(retry.submitted(0,i*1000,true)==Result::Ok);
  }
  CHECK(retry.candidate(5000,frame)==Result::RetryExhausted);
  windowAck(ack,2,0,1);
  CHECK(retry.acknowledge(ack,sizeof(ack))==Result::Invalid);
  windowAck(ack,2,0,2);
  CHECK(retry.acknowledge(ack,sizeof(ack))==Result::Invalid);
  return 0;
}

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
  CHECK(highSpeedWindowCase(20)==0);
  CHECK(highSpeedWindowCase(BleDownloadProtocol::MaxFrameSize)==0);
  CHECK(highSpeedFailureCase()==0);
  return 0;
}
