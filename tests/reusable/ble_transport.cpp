#define TACTICPOD_BLE 1
#define TACTICPOD_REUSABLE 1
#define TACTICPOD_TCB3_FLASH_ENABLED 1
#include <stdio.h>
#include "../../src/ble_runtime.cpp"

uint32_t mockTime=0;
unsigned mockRedWrites=0;
MockSerial Serial;
static unsigned cancelled=0,released=0,finished=0;
static bool listAvailable=true;
static bool listPending=false;
namespace BatteryRuntime {void capture(Report& report){report=Report{};}}
namespace Tcb3Session {
bool bleBeginList(bool* notReady){if(notReady)*notReady=listPending;return listAvailable;}
bool bleService(unsigned){return false;}
bool bleListDone(){return true;}
bool bleListFailed(){return false;}
bool bleListOverflow(){return false;}
uint32_t bleIncompleteCount(){return 0;}
unsigned bleSessionCount(){return 0;}
const Tcb3::ManifestEntry* bleSession(unsigned){return nullptr;}
bool bleBeginSession(uint32_t,uint32_t){return true;}
const uint8_t* blePacket(unsigned& length){length=0;return nullptr;}
void bleReleasePacket(){++released;}
bool bleTransferDone(){return true;}
bool bleTransferFailed(){return false;}
void bleFinishList(){++finished;}
void bleFinishSession(){++finished;}
void bleCancel(){++cancelled;}
}
#define CHECK(condition) do {if(!(condition))return __LINE__;} while(0)

static void resetMocks(){
  mockNotifyCalls=mockResets=cancelled=released=finished=0;
  mockSerialPins=0;
  mockLink=mockNotifyEnabled=mockNotifyAccepted=true;mockMtu=23;
  listAvailable=true;listPending=false;
  started=true;failed=false;events.store(0);controlReady.store(false);
  Bluefruit.Advertising.running=false;
}
static void beginMessage(Adapter& target){
  uint8_t payload[280];
  for(unsigned i=0;i<sizeof(payload);++i)payload[i]=uint8_t(i*37U);
  target.transfer=TransferState::Downloading;target.transferId=2;
  target.setMessage(kTransportPacket,payload,sizeof(payload));
}
static void request(Adapter& target,uint8_t opcode,uint16_t id,const uint8_t* fields=nullptr,unsigned count=0){
  uint8_t bytes[14]{kProtocolVersion,opcode};
  write16(bytes+2,id);
  if(count)memcpy(bytes+4,fields,count);
  write16(bytes+4+count,crc16(bytes,4+count));
  onControlWrite(0,&control,bytes,uint16_t(count+6));
  target.processControl();
}
static void ack(Adapter& target,unsigned index){
  const uint8_t* frame=mockNotifications[index].bytes;
  uint8_t fields[8];write32(fields,read32(frame+4));write16(fields+4,read16(frame+8));
  write16(fields+6,read16(frame+mockNotifications[index].length-2));
  request(target,0x03,read16(frame+2),fields,sizeof(fields));
}
static bool sameFrame(unsigned a,unsigned b){
  return mockNotifications[a].length==mockNotifications[b].length &&
    !memcmp(mockNotifications[a].bytes,mockNotifications[b].bytes,mockNotifications[a].length);
}

extern "C" unsigned fastAckCase(unsigned largeMtu){
  resetMocks();mockMtu=largeMtu?247:23;Adapter target;beginMessage(target);
  uint8_t collected[280]{};unsigned offset=0;uint32_t now=100;
  while(target.messageReady){
    const unsigned before=mockNotifyCalls;
    target.transmit(now);
    CHECK(mockNotifyCalls==before+1 && target.awaitingAck);
    const MockNotification& notification=mockNotifications[before];
    const uint8_t* frame=notification.bytes;
    CHECK(notification.accepted && notification.length<=unsigned(mockMtu-3));
    CHECK(frame[0]==2 && frame[1]==kTransportPacket && read16(frame+2)==2);
    CHECK(read32(frame+4)==0 && read16(frame+8)==offset && read16(frame+10)==280);
    CHECK(read16(frame+notification.length-2)==crc16(frame,notification.length-2));
    CHECK(offset+frame[12]<=sizeof(collected));
    memcpy(collected+offset,frame+13,frame[12]);offset+=frame[12];
    target.transmit(now+1);
    CHECK(mockNotifyCalls==before+1);
    ack(target,before);
    CHECK(!target.awaitingAck && target.messageOffset==(offset==280?0:offset));
    now+=10;
  }
  CHECK(mockNotifyCalls==(largeMtu?2U:56U) && released==1 && target.messageSequence==1);
  for(unsigned i=0;i<sizeof(collected);++i)CHECK(collected[i]==uint8_t(i*37U));
  target.setMessage(kTransferEnd,collected,12);target.transmit(now);
  CHECK(mockNotifyCalls==(largeMtu?3U:57U) && target.awaitingAck);
  return 0;
}

extern "C" unsigned slowAckCase(unsigned){
  resetMocks();Adapter target;beginMessage(target);target.transmit(100);
  CHECK(mockNotifyCalls==1 && target.awaitingAck);
  target.transmit(1099);CHECK(mockNotifyCalls==1);
  target.transmit(1100);CHECK(mockNotifyCalls==2 && sameFrame(0,1));
  uint8_t invalid[8]{};
  request(target,0x03,3,invalid,sizeof(invalid));
  request(target,0x03,2,invalid,sizeof(invalid));
  write16(invalid+6,target.frameCrc);
  write32(invalid,1);request(target,0x03,2,invalid,sizeof(invalid));
  write32(invalid,0);write16(invalid+4,5);request(target,0x03,2,invalid,sizeof(invalid));
  CHECK(target.awaitingAck && target.messageOffset==0);
  ack(target,0);CHECK(!target.awaitingAck && target.messageOffset==5);
  target.transmit(1110);
  CHECK(mockNotifyCalls==3 && read16(mockNotifications[2].bytes+8)==5);
  ack(target,1);
  CHECK(target.awaitingAck && target.messageOffset==5 && target.messageSequence==0);
  target.transmit(2109);CHECK(mockNotifyCalls==3);
  target.transmit(2110);CHECK(mockNotifyCalls==4 && sameFrame(2,3));
  ack(target,3);
  CHECK(!target.awaitingAck && target.messageOffset==10 && !released);
  return 0;
}

extern "C" unsigned backpressureCase(unsigned wrapping){
  resetMocks();Adapter target;beginMessage(target);mockNotifyEnabled=false;
  target.transmit(100);CHECK(mockNotifyCalls==0);
  mockNotifyEnabled=true;mockNotifyAccepted=false;
  const uint32_t first=wrapping?0xfffffff0U:100U;
  target.transmit(first);
  CHECK(mockNotifyCalls==1 && !target.awaitingAck && target.messageOffset==0 && !target.frameAttempts);
  target.transmit(first+99);CHECK(mockNotifyCalls==1);
  mockNotifyAccepted=true;target.transmit(first+100);
  CHECK(mockNotifyCalls==2 && sameFrame(0,1) && target.awaitingAck && target.frameAttempts==1);
  ack(target,1);target.transmit(first+110);
  CHECK(mockNotifyCalls==3 && read16(mockNotifications[2].bytes+8)==5);
  return 0;
}

extern "C" unsigned retryBoundCase(unsigned queueFull){
  resetMocks();Adapter target;beginMessage(target);
  mockNotifyAccepted=!queueFull;
  const uint32_t interval=queueFull?100:1000;
  for(unsigned i=0;i<5;++i){
    target.transmit(100+i*interval);
    CHECK(mockNotifyCalls==i+1);
    CHECK(!i || sameFrame(0,i));
    if(i<4)CHECK(target.transfer==TransferState::Downloading);
  }
  if(!queueFull){
    target.transmit(5099);CHECK(mockNotifyCalls==5);
    target.transmit(5100);CHECK(mockNotifyCalls==5);
  }
  CHECK(target.transfer==TransferState::SendingError && cancelled==1);
  CHECK(target.messageType==kError && read16(target.message)==kAckTimeout && !released);
  const uint32_t errorStart=5200;
  for(unsigned i=0;i<5;++i)target.transmit(errorStart+i*interval);
  if(!queueFull)target.transmit(errorStart+5*interval);
  CHECK(target.transfer==TransferState::Idle && cancelled==2 && !target.awaitingAck);
  CHECK(mockNotifyCalls==10);
  return 0;
}

extern "C" unsigned cancelCase(unsigned backpressured){
  resetMocks();Adapter target;beginMessage(target);mockNotifyAccepted=!backpressured;
  target.transmit(100);
  CHECK(target.awaitingAck==!backpressured);
  request(target,0x04,3);
  CHECK(target.transfer==TransferState::Downloading && target.awaitingAck==!backpressured);
  mockNotifyAccepted=true;
  request(target,0x04,2);
  CHECK(target.transfer==TransferState::SendingCancel && !target.awaitingAck && finished==1);
  ack(target,0);
  CHECK(target.transfer==TransferState::SendingCancel && !target.awaitingAck && !released);
  target.transmit(110);
  CHECK(mockNotifyCalls==2 && mockNotifications[1].bytes[1]==kCancelled);
  ack(target,1);
  CHECK(target.transfer==TransferState::Idle && !target.messageReady && !target.awaitingAck);
  target.transmit(120);CHECK(mockNotifyCalls==2);
  request(target,0x01,9);target.service(120);
  CHECK(mockNotifyCalls==3 && mockNotifications[2].bytes[1]==kListBegin);
  CHECK(read16(mockNotifications[2].bytes+2)==9 && read32(mockNotifications[2].bytes+4)==0);
  ack(target,0);CHECK(target.awaitingAck && target.messageOffset==0);
  return 0;
}

extern "C" unsigned disconnectCase(unsigned){
  resetMocks();Adapter target;BlePhase1::Runtime<Adapter> lifecycle;BleMode::Inputs in;
  in.doubleTap=true;lifecycle.update(0,in,target);
  in.doubleTap=false;lifecycle.update(1,in,target);
  CHECK(lifecycle.state()==BleMode::State::Connected);
  beginMessage(target);lifecycle.update(100,in,target);
  CHECK(mockNotifyCalls==1 && target.awaitingAck);
  mockLink=false;onDisconnect(0,0);lifecycle.update(110,in,target);
  CHECK(lifecycle.state()==BleMode::State::Stopping);
  CHECK(target.transfer==TransferState::Idle && !target.awaitingAck && !target.frameReady);
  lifecycle.update(120,in,target);lifecycle.update(130,in,target);
  CHECK(mockNotifyCalls==1 && mockResets==1 && !released);
  return 0;
}

extern "C" unsigned cacheNotReadyCase(unsigned failed){
  resetMocks();Adapter target;listAvailable=false;listPending=!failed;
  request(target,0x01,2);target.service(100);
  CHECK(mockNotifyCalls==1 && target.transfer==TransferState::SendingError);
  CHECK(mockNotifications[0].bytes[1]==kError &&
    read16(mockNotifications[0].bytes+13)==(failed?kManifestFailure:kManifestNotReady));
  ack(target,0);CHECK(target.transfer==TransferState::Idle);
  listAvailable=true;listPending=false;request(target,0x01,3);target.service(110);
  CHECK(mockNotifyCalls==2 && mockNotifications[1].bytes[1]==kListBegin);
  CHECK(read16(mockNotifications[1].bytes+2)==3);
  return 0;
}
