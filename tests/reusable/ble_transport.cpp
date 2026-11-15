#define TACTICPOD_BLE 1
#define TACTICPOD_REUSABLE 1
#define TACTICPOD_TCB3_FLASH_ENABLED 1
#include <stdio.h>
#include "../../src/ble_runtime.cpp"

uint32_t mockTime=0;
unsigned mockRedWrites=0;
MockSerial Serial;
static unsigned cancelled=0,released=0,finished=0;
static unsigned cacheServiceCalls=0,cacheServiceBudget=0;
static bool listAvailable=true;
static bool listPending=false;
static bool provisionalEnabled=false,validationAllowed=false,validationDone=false,validationFailed=false;
static bool validationServiceProgress=false;
static bool provisionalAdmission=true;
static uint16_t discoveryError=4;
static bool streamEnabled=false;
static bool exporterOwned=false;
static unsigned streamIndex=0;
static uint8_t streamPackets[3][280]{};
static const unsigned streamLengths[]={40,280,24};
namespace BatteryRuntime {void capture(Report& report){report=Report{};}}
namespace Tcb3Session {
bool bleBeginList(bool* notReady){if(notReady)*notReady=listPending;return listAvailable;}
bool bleEnableProvisional(){if(!provisionalAdmission)return false;provisionalEnabled=true;return true;}
void invalidateBleManifest(){provisionalEnabled=false;}
bool bleBeginValidation(uint32_t,uint32_t){return provisionalEnabled && validationAllowed;}
bool bleValidationDone(){return validationDone;}
bool bleValidationFailed(){return validationFailed;}
bool bleService(unsigned steps){++cacheServiceCalls;cacheServiceBudget=steps;return validationServiceProgress;}
bool bleListDone(){return true;}
bool bleListFailed(){return false;}
uint16_t bleDiscoveryError(){return discoveryError;}
bool bleListOverflow(){return false;}
uint32_t bleIncompleteCount(){return 0;}
unsigned bleSessionCount(){return 0;}
const Tcb3::ManifestEntry* bleSession(unsigned){return nullptr;}
bool bleBeginSession(uint32_t,uint32_t){exporterOwned=true;return true;}
const uint8_t* blePacket(unsigned& length){
  if(streamEnabled && streamIndex<3){length=streamLengths[streamIndex];return streamPackets[streamIndex];}
  length=0;return nullptr;
}
void bleReleasePacket(){++released;if(streamEnabled && streamIndex<3)++streamIndex;}
bool bleTransferDone(){return !streamEnabled || streamIndex==3;}
bool bleTransferFailed(){return false;}
void bleFinishList(){++finished;}
void bleFinishSession(){exporterOwned=false;++finished;}
void bleCancel(){exporterOwned=false;++cancelled;}
}
#define CHECK(condition) do {if(!(condition))return __LINE__;} while(0)

static void resetMocks(){
  mockNotifyCalls=mockResets=cancelled=released=finished=0;
  cacheServiceCalls=cacheServiceBudget=0;
  mockNotifyDurationUs=mockReadReplies=mockReadStatus=mockReadOffset=0;
  mockTime=mockSubUs=0;
  BlePerformance::counters()=BlePerformance::Counters{};
  performanceReadReady.store(false);
  linkReadReady.store(false);
  speedReadReady.store(false);speedAckHead.store(0);speedAckTail.store(0);speedAckOverflow.store(false);
  notificationCredit.store(true);
  mockSerialPins=0;
  mockLink=mockNotifyEnabled=mockNotifyAccepted=true;mockMtu=23;
  mockConnectionAvailable=true;mockDataLength=27;mockInterval=24;mockPhy=1;
  mockFeatureBeginFailure=mockFeatureWriteFailure=false;mockFeatureLength=mockProtocolLength=0;
  memset(mockFeatureValue,0,sizeof(mockFeatureValue));memset(mockProtocolValue,0,sizeof(mockProtocolValue));
  listAvailable=true;listPending=false;
  provisionalEnabled=validationAllowed=validationDone=validationFailed=false;discoveryError=4;
  validationServiceProgress=false;
  provisionalAdmission=true;
  streamEnabled=exporterOwned=false;streamIndex=0;
  started=true;failed=false;events.store(0);
  controlHead.store(0);controlTail.store(0);controlOverflow.store(false);
  Bluefruit.Advertising.running=false;
}
static void request(Adapter&,uint8_t,uint16_t,const uint8_t*,unsigned);
static void ack(Adapter&,unsigned);
static void speedAckRequest(Adapter& target,uint16_t id,uint32_t next,uint16_t bitmap){
  uint8_t bytes[12]{3,1};write16(bytes+2,id);write32(bytes+4,next);
  write16(bytes+8,bitmap);write16(bytes+10,crc16(bytes,10));
  onSpeedAck(0,&speedAck,bytes,sizeof(bytes));target.processSpeedAck();
}

extern "C" unsigned highSpeedTransferCase(unsigned loseCompletionAck){
  resetMocks();Adapter target;target.speedAvailable=true;streamEnabled=true;
  for(unsigned packet=0;packet<3;++packet){
    for(unsigned i=0;i<streamLengths[packet];++i)streamPackets[packet][i]=uint8_t(packet*31U+i);
    memcpy(streamPackets[packet],"TPX3",4);
    streamPackets[packet][5]=packet==0?1:packet==1?2:4;
  }
  const uint8_t mode[]={3,4};request(target,0x05,2,mode,sizeof(mode));
  CHECK(target.transfer==TransferState::Selecting && target.speedStatus==1);
  target.transmit(1);CHECK(mockNotifyCalls==1 && mockNotifications[0].bytes[1]==kModeSelected);
  ack(target,0);CHECK(target.speedStatus==2 && target.transfer==TransferState::Idle);
  uint8_t fields[8]{};write32(fields,1);write32(fields+4,7);
  request(target,0x02,2,fields,sizeof(fields));
  CHECK(target.speedActive && target.speedStatus==3);
  unsigned seen=1;uint32_t now=10;uint8_t reconstructed[344]{};unsigned used=0;
  while(target.speedWindow.next()<4 && now<1000){
    notificationCredit.store(true);target.service(now++);
  }
  CHECK(mockNotifyCalls==5 && target.speedWindow.base()==0 && !finished);
  for(unsigned index=1;index<5;++index){
    const uint8_t* bytes=mockNotifications[index].bytes;
    CHECK(bytes[0]==3 && read32(bytes+4)==index-1);
    if(index!=2){memcpy(reconstructed+(index-1)*9,bytes+9,bytes[8]);}
  }
  speedAckRequest(target,2,1,0x0006);
  CHECK(target.speedWindow.base()==1 && !finished);
  notificationCredit.store(true);target.service(1020);
  CHECK(mockNotifyCalls==6 && read32(mockNotifications[5].bytes+4)==1);
  memcpy(reconstructed+9,mockNotifications[5].bytes+9,mockNotifications[5].bytes[8]);
  speedAckRequest(target,2,4,0);used=36;
  seen=6;now=1030;
  while(target.transfer==TransferState::Downloading && now<10000){
    notificationCredit.store(true);target.service(now++);
    while(seen<mockNotifyCalls){
      const MockNotification& notification=mockNotifications[seen++];
      CHECK(notification.bytes[0]==3 && read32(notification.bytes+4)==target.speedWindow.base());
      CHECK(used+notification.bytes[8]<=sizeof(reconstructed));
      memcpy(reconstructed+used,notification.bytes+9,notification.bytes[8]);used+=notification.bytes[8];
      speedAckRequest(target,2,read32(notification.bytes+4)+1,0);
    }
  }
  CHECK(used==sizeof(reconstructed) && released==3 && !finished && exporterOwned);
  unsigned offset=0;
  for(unsigned packet=0;packet<3;++packet){
    CHECK(memcmp(reconstructed+offset,streamPackets[packet],streamLengths[packet])==0);
    offset+=streamLengths[packet];
  }
  CHECK(target.transfer==TransferState::SendingEnd && target.speedStatus==3);
  bool lost=false;unsigned lostIndex=0;
  while(target.transfer==TransferState::SendingEnd && now<11000){
    const unsigned before=mockNotifyCalls;target.service(now++);
    if(mockNotifyCalls>before){
      const MockNotification& notification=mockNotifications[before];
      if(loseCompletionAck && !lost && read16(notification.bytes+8)+notification.bytes[12]==12){
        lost=true;lostIndex=before;now+=1000;
        CHECK(exporterOwned && !finished && target.speedStatus==3);
      }else{
        if(lost){
          CHECK(notification.length==mockNotifications[lostIndex].length);
          CHECK(!memcmp(notification.bytes,mockNotifications[lostIndex].bytes,notification.length));
        }
        ack(target,before);
      }
    }
  }
  CHECK(lost==bool(loseCompletionAck));
  CHECK(target.transfer==TransferState::Idle && target.speedStatus==4 && finished==1 && !exporterOwned);
  return 0;
}

extern "C" unsigned highSpeedFailureCase(unsigned scenario){
  resetMocks();Adapter target;target.speedAvailable=true;target.speedStatus=2;target.speedId=2;
  streamEnabled=true;
  for(unsigned packet=0;packet<3;++packet){
    memset(streamPackets[packet],0,streamLengths[packet]);
    memcpy(streamPackets[packet],"TPX3",4);
    streamPackets[packet][5]=packet==0?1:packet==1?2:4;
  }
  CHECK(target.startDownload(2,1,7) && target.speedActive);
  if(scenario==0){
    mockNotifyAccepted=false;
    for(unsigned i=0;i<8 && target.transfer==TransferState::Downloading;++i)
      target.service(100+i*100);
    CHECK(target.transfer==TransferState::SendingError && cancelled==1 && !finished);
    CHECK(target.speedStatus==5 && !target.speedActive);
  }else if(scenario==1){
    notificationCredit.store(false);
    target.service(10);CHECK(mockNotifyCalls==0 && target.speedActive);
    target.service(5010);
    CHECK(target.transfer==TransferState::SendingError && cancelled==1 && !finished);
  }else if(scenario==6){
    target.service(10);
    CHECK(mockNotifyCalls==1 && target.speedWindow.base()==0);
    speedAckRequest(target,3,1,0);
    CHECK(target.speedWindow.base()==0 && target.speedActive);
    speedAckRequest(target,2,1,0);
    CHECK(target.speedWindow.base()==1 && target.speedActive);
    speedAckRequest(target,2,0,0);
    CHECK(target.speedWindow.base()==1 && target.speedActive);
    speedAckRequest(target,2,3,0);
    CHECK(target.transfer==TransferState::SendingError && cancelled==1 && !finished);
  }else if(scenario==7 || scenario==8){
    target.service(10);CHECK(exporterOwned && target.speedActive);
    uint8_t bytes[12]{3,1};write16(bytes+2,2);write32(bytes+4,1);
    write16(bytes+10,crc16(bytes,10));
    if(scenario==7)bytes[11]^=1;
    onSpeedAck(0,&speedAck,bytes,scenario==7?sizeof(bytes):sizeof(bytes)-1);
    target.processSpeedAck();
    CHECK(target.transfer==TransferState::SendingError && target.speedStatus==5);
    CHECK(!target.speedActive && !exporterOwned && cancelled==1 && !finished);
  }else if(scenario==4 || scenario==5){
    uint8_t bytes[12]{3,1};write16(bytes+2,2);write16(bytes+10,crc16(bytes,10));
    for(unsigned i=0;i<(scenario==4?5U:4U);++i)onSpeedAck(0,&speedAck,bytes,sizeof(bytes));
    if(scenario==4){
      target.processSpeedAck();
      CHECK(target.transfer==TransferState::SendingError && cancelled==1 && !finished);
      CHECK(target.speedStatus==5 && !target.speedActive);
    }else{
      for(unsigned i=0;i<4;++i)target.processSpeedAck();
      CHECK(target.transfer==TransferState::Downloading && target.speedActive && !cancelled);
      CHECK(speedAckHead.load()==speedAckTail.load());
    }
  }else{
    target.service(10);CHECK(mockNotifyCalls==1 && target.speedActive);
    if(scenario==2){
      CHECK(target.cancel(2) && target.transfer==TransferState::SendingCancel && finished==1);
      speedAckRequest(target,2,1,0);
      CHECK(target.transfer==TransferState::SendingCancel && target.speedStatus==5);
      target.transmit(20);ack(target,1);
      CHECK(target.transfer==TransferState::Idle);
    }else{
      target.abortTransfer();
      CHECK(target.transfer==TransferState::Idle && cancelled==1 && !finished && target.speedStatus==5);
    }
  }
  return 0;
}

extern "C" unsigned highSpeedLifecycleCase(unsigned scenario){
  resetMocks();Adapter target;BlePhase1::Runtime<Adapter> lifecycle;BleMode::Inputs in;
  in.doubleTap=true;lifecycle.update(0,in,target);
  in.doubleTap=false;lifecycle.update(1,in,target);
  CHECK(lifecycle.state()==BleMode::State::Connected);
  CHECK(target.speedAvailable);
  const uint8_t mode[]={3,4};request(target,0x05,2,mode,sizeof(mode));
  target.transmit(2);ack(target,0);
  CHECK(target.speedStatus==2 && !exporterOwned);
  if(scenario%3){
    streamEnabled=true;
    for(unsigned packet=0;packet<3;++packet){
      memset(streamPackets[packet],0,streamLengths[packet]);
      memcpy(streamPackets[packet],"TPX3",4);
      streamPackets[packet][5]=packet==0?1:packet==1?2:4;
    }
    CHECK(target.startDownload(2,1,7) && exporterOwned);
    uint32_t now=10;
    while(target.speedWindow.next()<4 && now<100){
      notificationCredit.store(true);target.service(now++);
    }
    CHECK(target.speedWindow.next()==4 && target.speedWindow.base()==0 && exporterOwned);
    if(scenario==2 || scenario==5){
      speedAckRequest(target,2,4,0);
      while(target.transfer==TransferState::Downloading && now<1000){
        notificationCredit.store(true);target.service(now++);
        const unsigned index=mockNotifyCalls-1;
        if(mockNotifications[index].bytes[0]==3 && read32(mockNotifications[index].bytes+4)>=target.speedWindow.base())
          speedAckRequest(target,2,read32(mockNotifications[index].bytes+4)+1,0);
      }
      CHECK(target.transfer==TransferState::SendingEnd && exporterOwned && !finished);
      target.transmit(1000);
      CHECK(target.awaitingAck && target.speedStatus==3);
    }
  }
  const unsigned notifications=mockNotifyCalls;
  if(scenario>=3){
    CHECK(target.cancel(2) && target.speedStatus==5 && !exporterOwned);
    CHECK(target.transfer==TransferState::SendingCancel && !target.speedActive);
    speedAckRequest(target,2,1,0);
    CHECK(target.transfer==TransferState::SendingCancel && target.speedStatus==5);
    target.transmit(1010);ack(target,notifications);
    CHECK(target.transfer==TransferState::Idle);
    return 0;
  }
  uint8_t queued[BleHighSpeed::AckSize]{3,1};write16(queued+2,2);
  write32(queued+4,UINT32_MAX);write16(queued+10,crc16(queued,10));
  onSpeedAck(0,&speedAck,queued,sizeof(queued));
  mockLink=false;onDisconnect(0,0);
  CHECK(target.speedStatus==(scenario?3:2) && exporterOwned==bool(scenario));
  lifecycle.update(1100,in,target);
  CHECK(lifecycle.state()==BleMode::State::Stopping && target.transfer==TransferState::Idle);
  CHECK(target.speedStatus==5 && !target.speedActive && !exporterOwned && cancelled && !finished);
  CHECK(!target.awaitingAck && !target.frameReady && !target.messageReady);
  CHECK(speedAckHead.load()==speedAckTail.load() && !speedAckOverflow.load());
  target.processSpeedAck();target.service(1110);
  lifecycle.update(1120,in,target);lifecycle.update(1130,in,target);
  CHECK(mockNotifyCalls==notifications && mockResets==1 && target.speedStatus==5);
  return 0;
}

static void beginMessage(Adapter& target){
  uint8_t payload[280];
  for(unsigned i=0;i<sizeof(payload);++i)payload[i]=uint8_t(i*37U);
  target.transfer=TransferState::Downloading;target.transferId=2;
  target.setMessage(kTransportPacket,payload,sizeof(payload));
}
static void enqueueRequest(uint8_t opcode,uint16_t id,const uint8_t* fields=nullptr,unsigned count=0){
  uint8_t bytes[14]{kProtocolVersion,opcode};
  write16(bytes+2,id);
  if(count)memcpy(bytes+4,fields,count);
  write16(bytes+4+count,crc16(bytes,4+count));
  onControlWrite(0,&control,bytes,uint16_t(count+6));
}
static void request(Adapter& target,uint8_t opcode,uint16_t id,const uint8_t* fields=nullptr,unsigned count=0){
  enqueueRequest(opcode,id,fields,count);
  target.processControl();
}
static void enqueueAck(unsigned index){
  const uint8_t* frame=mockNotifications[index].bytes;
  uint8_t fields[8];write32(fields,read32(frame+4));write16(fields+4,read16(frame+8));
  write16(fields+6,read16(frame+mockNotifications[index].length-2));
  enqueueRequest(0x03,read16(frame+2),fields,sizeof(fields));
}
static void ack(Adapter& target,unsigned index){
  enqueueAck(index);target.processControl();
}
static bool sameFrame(unsigned a,unsigned b){
  return mockNotifications[a].length==mockNotifications[b].length &&
    !memcmp(mockNotifications[a].bytes,mockNotifications[b].bytes,mockNotifications[a].length);
}

extern "C" unsigned controlQueueCase(unsigned scenario){
  resetMocks();Adapter target;target.featureAvailable=true;
  if(scenario==0 || scenario==1 || scenario==3){
    mockMtu=247;
    const uint8_t mode[]={1};request(target,0x06,1,mode,sizeof(mode));
    CHECK(target.transfer==TransferState::SelectingDiscovery);
    target.transmit(1);CHECK(mockNotifyCalls==1 && target.awaitingAck);
    if(scenario==3){
      enqueueAck(0);
      for(unsigned i=1;i<kControlQueueSize;++i)enqueueRequest(0x01,uint16_t(i+1));
      enqueueRequest(0x01,9);
      CHECK(controlOverflow.load() && target.awaitingAck && provisionalEnabled);
      const uint32_t head=controlHead.load();enqueueRequest(0x04,1);
      CHECK(controlHead.load()==head);
      target.service(2);
      CHECK(failed && target.controlOverflowHandled && !provisionalEnabled);
      CHECK(target.transfer==TransferState::Idle && !target.messageReady && !target.awaitingAck);
      CHECK(controlHead.load()==controlTail.load() && !BlePerformance::counters().listRequests);
      const unsigned stops=cancelled;target.processControl();
      CHECK(cancelled==stops && mockNotifyCalls==1);
      return 0;
    }
    if(scenario==0){
      mockTime=10;enqueueAck(0);mockTime=20;enqueueRequest(0x01,2);
      mockTime=30;
      target.processControl();
      CHECK(target.transfer==TransferState::Idle && provisionalEnabled);
      mockTime=40;
      target.processControl();
      CHECK(target.transfer==TransferState::Scanning && target.transferId==2);
      CHECK(BlePerformance::counters().listRequests==1);
      mockTime=50;target.service(50);
      CHECK(BlePerformance::counters().listToBeginUs==30000);
      return 0;
    }
    ack(target,0);
    validationAllowed=true;
    uint8_t fields[8];write32(fields,12);write32(fields+4,7);
    request(target,0x07,2,fields,sizeof(fields));target.transmit(2);
    CHECK(mockNotifyCalls==2 && mockNotifications[1].bytes[1]==kValidationProgress);
    ack(target,1);validationDone=true;target.service(3);
    CHECK(mockNotifyCalls==3 && mockNotifications[2].bytes[1]==kValidationComplete);
    enqueueAck(2);enqueueRequest(0x02,3,fields,sizeof(fields));
    target.processControl();
    CHECK(target.transfer==TransferState::Idle);
    target.processControl();
    CHECK(target.transfer==TransferState::Downloading && target.transferId==3 && exporterOwned);
    return 0;
  }
  if(scenario==2){
    beginMessage(target);target.transmit(1);
    uint8_t bad[14]{2,3,2,0};write16(bad+12,uint16_t(crc16(bad,12)^1U));
    const uint8_t* frame=mockNotifications[0].bytes;
    uint8_t fields[8];write32(fields,read32(frame+4));write16(fields+4,read16(frame+8));
    write16(fields+6,read16(frame+mockNotifications[0].length-2));
    onControlWrite(0,&control,bad,sizeof(bad));
    enqueueRequest(0x03,3,fields,sizeof(fields));enqueueAck(0);enqueueRequest(0x04,2);
    target.processControl();
    CHECK(target.awaitingAck && target.messageOffset==0 && (events.load()&kInvalid));
    target.processControl();
    CHECK(target.awaitingAck && target.messageOffset==0);
    target.processControl();
    CHECK(!target.awaitingAck && target.messageOffset==5);
    target.processControl();
    CHECK(target.transfer==TransferState::SendingCancel && finished==1 && !released);
    return 0;
  }
  if(scenario==4 || scenario==7){
    BlePhase1::Runtime<Adapter> lifecycle;BleMode::Inputs in;
    in.doubleTap=true;lifecycle.update(0,in,target);
    in.doubleTap=false;lifecycle.update(1,in,target);
    CHECK(lifecycle.state()==BleMode::State::Connected);
    if(scenario==4){
      enqueueRequest(0x01,4);
      mockLink=false;onDisconnect(0,0);lifecycle.update(2,in,target);
      CHECK(!BlePerformance::counters().listRequests && controlHead.load()==controlTail.load());
    }else{
      for(unsigned i=0;i<kControlQueueSize+1;++i)enqueueRequest(0x01,uint16_t(i+1));
      lifecycle.update(2,in,target);
      CHECK(failed && controlOverflow.load() && !BlePerformance::counters().listRequests);
    }
    CHECK(lifecycle.state()==BleMode::State::Stopping);
    lifecycle.update(3,in,target);lifecycle.update(4,in,target);
    CHECK(mockResets==1 && controlHead.load()==controlTail.load());
    return 0;
  }
  if(scenario==5){
    target.speedAvailable=true;target.speedStatus=2;target.speedId=2;streamEnabled=true;
    CHECK(target.startDownload(2,1,7) && target.speedActive && exporterOwned);
    enqueueRequest(0x04,2);
    for(unsigned i=1;i<kControlQueueSize+1;++i)enqueueRequest(0x01,uint16_t(i+1));
    target.service(2);
    CHECK(failed && controlOverflow.load() && !exporterOwned && !target.speedActive);
    CHECK(target.speedStatus==5 && target.transfer==TransferState::Idle && !finished && cancelled==1);
    return 0;
  }
  if(scenario==6){
    controlHead.store(UINT32_MAX-1);controlTail.store(UINT32_MAX-1);
    uint8_t shortMessage[]={2},oversize[17]{};
    onControlWrite(0,&control,shortMessage,0);
    onControlWrite(0,&control,oversize,sizeof(oversize));
    CHECK(controlHead.load()==UINT32_MAX-1 && !controlOverflow.load());
    for(unsigned i=0;i<kControlQueueSize;++i)onControlWrite(0,&control,shortMessage,sizeof(shortMessage));
    CHECK(controlHead.load()==2 && !controlOverflow.load());
    for(unsigned i=0;i<kControlQueueSize;++i)target.processControl();
    CHECK(controlTail.load()==controlHead.load() && !failed);
    return 0;
  }
  if(scenario==8){
    mockMtu=247;request(target,0x01,10);target.service(1);
    CHECK(mockNotifyCalls==1 && mockNotifications[0].bytes[1]==kListBegin);
    ack(target,0);target.service(2);
    CHECK(mockNotifyCalls==2 && mockNotifications[1].bytes[1]==kListEnd);
    enqueueAck(1);enqueueRequest(0x01,11);
    target.processControl();CHECK(target.transfer==TransferState::Idle);
    target.processControl();
    CHECK(target.transfer==TransferState::Scanning && target.transferId==11);
    target.service(3);
    CHECK(mockNotifyCalls==3 && mockNotifications[2].bytes[1]==kListBegin);
    CHECK(read16(mockNotifications[2].bytes+2)==11);
    return 0;
  }
  if(scenario==9){
    mockMtu=247;target.speedAvailable=true;
    const uint8_t mode[]={3,4};request(target,0x05,15,mode,sizeof(mode));
    target.transmit(1);CHECK(mockNotifyCalls==1 && target.awaitingAck);
    uint8_t fields[8];write32(fields,1);write32(fields+4,7);
    enqueueAck(0);enqueueRequest(0x02,15,fields,sizeof(fields));
    target.processControl();CHECK(target.speedStatus==2 && target.transfer==TransferState::Idle);
    target.processControl();
    CHECK(target.transfer==TransferState::Downloading && target.speedActive && target.speedId==15 && exporterOwned);
    return 0;
  }
  CHECK(false);
  return 0;
}

extern "C" unsigned featureCapabilityCase(unsigned scenario){
  resetMocks();Adapter target;
  mockFeatureBeginFailure=scenario==1;
  mockFeatureWriteFailure=scenario==2;
  CHECK(target.begin() && target.speedAvailable);
  CHECK(mockProtocolLength==2 && mockProtocolValue[0]==2 && mockProtocolValue[1]==0);
  CHECK(!provisionalEnabled);
  if(scenario==1 || scenario==2){
    CHECK(!target.featureAvailable && !mockFeatureLength);
    const uint8_t speedMode[]={3,4};request(target,0x05,3,speedMode,sizeof(speedMode));
    CHECK(target.transfer==TransferState::Selecting && target.speedStatus==1);
    target.transmit(1);ack(target,0);
    CHECK(target.transfer==TransferState::Idle && target.speedStatus==2);
    target.abortTransfer();
    CHECK(target.startList(7));
    target.abortTransfer();
    const uint8_t mode[]={1};request(target,0x06,8,mode,sizeof(mode));
    CHECK(target.transfer==TransferState::SendingError && target.messageType==kError);
    CHECK(read16(target.message)==kBadCommand && !provisionalEnabled);
    return 0;
  }
  const uint8_t expected[]={0x54,0x50,0x46,0x31,0x01,0x00,0x01,0x00,0x00,0x00,0x44,0x02};
  CHECK(target.featureAvailable && mockFeatureLength==sizeof(expected));
  CHECK(!memcmp(mockFeatureValue,expected,sizeof(expected)));
  CHECK(read16(mockFeatureValue+10)==crc16(mockFeatureValue,10));
  ble_gatts_evt_read_t read{};onSpeedRead(0,&speedCapability,&read);target.service(1);
  CHECK(!memcmp(mockReadValue,"TPH1",4) && mockReadValue[4]==3 && mockReadValue[5]==4);
  CHECK(!mockReadValue[6] && !mockReadValue[7] && !read16(mockReadValue+8));
  CHECK(read16(mockReadValue+10)==244 && !read16(mockReadValue+12));
  CHECK(read16(mockReadValue+14)==crc16(mockReadValue,14));
  if(scenario==0)return 0;
  if(scenario==6){
    uint8_t invalid[]={2,6,1,0,1,0,0};
    onControlWrite(0,&control,invalid,sizeof(invalid));target.processControl();
    CHECK(target.messageType==kError && read16(target.message)==kBadProtocol);
  }else if(scenario==5){
    request(target,0x06,1);
    CHECK(target.messageType==kError && read16(target.message)==kBadCommand);
  }else{
    const uint8_t mode[]={uint8_t(scenario==3?0:scenario==4?3:scenario==8?0x80:1)};
    request(target,0x06,1,mode,sizeof(mode));
    if(scenario==7){
      CHECK(target.transfer==TransferState::SelectingDiscovery && provisionalEnabled);
      target.transmit(2);CHECK(mockNotifications[0].bytes[1]==kDiscoverySelected);
      const uint8_t reply[]={0x02,0x88,0x01,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x01,0x00,0x01,0x01,0x83,0xb7};
      const uint8_t confirmation[]={0x02,0x03,0x01,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x83,0xb7,0x5b,0x0c};
      CHECK(mockNotifications[0].length==sizeof(reply) &&
        !memcmp(mockNotifications[0].bytes,reply,sizeof(reply)));
      uint8_t copy[sizeof(confirmation)];memcpy(copy,confirmation,sizeof(copy));
      onControlWrite(0,&control,copy,sizeof(copy));target.processControl();
      CHECK(target.transfer==TransferState::Idle);
      return 0;
    }
    CHECK(target.messageType==kError && read16(target.message)==kBadCommand);
  }
  CHECK(!provisionalEnabled && target.transfer==TransferState::SendingError);
  return 0;
}

extern "C" unsigned provisionalTransportCase(unsigned scenario){
  resetMocks();mockMtu=247;Adapter target;
  target.featureAvailable=true;
  uint8_t fields[8];write32(fields,12);write32(fields+4,7);
  if(scenario==0){
    validationAllowed=true;request(target,0x07,1,fields,sizeof(fields));
    CHECK(target.transfer==TransferState::SendingError && !provisionalEnabled);
    CHECK(target.messageType==kError && read16(target.message)==kNoSession);
    return 0;
  }
  if(scenario==9)provisionalAdmission=false;
  const uint8_t mode[]={1};request(target,0x06,1,mode,sizeof(mode));
  if(scenario==9){
    CHECK(!provisionalEnabled && target.transfer==TransferState::SendingError);
    CHECK(target.messageType==kError && read16(target.message)==kManifestFailure);
    return 0;
  }
  CHECK(target.transfer==TransferState::SelectingDiscovery && provisionalEnabled);
  if(scenario==7){
    CHECK(target.cancel(1) && !provisionalEnabled && target.transfer==TransferState::SendingCancel);
    return 0;
  }
  target.transmit(1);CHECK(mockNotifications[0].bytes[1]==kDiscoverySelected);
  ack(target,0);CHECK(provisionalEnabled && target.transfer==TransferState::Idle);
  if(scenario==5){
    listAvailable=false;discoveryError=8;request(target,0x01,2);
    CHECK(target.messageType==kError && read16(target.message)==8);
    return 0;
  }
  validationAllowed=true;request(target,0x07,2,fields,sizeof(fields));
  CHECK(target.transfer==TransferState::Validating && target.messageType==kValidationProgress);
  CHECK(read32(target.message)==12 && read32(target.message+4)==7);
  target.transmit(2);CHECK(mockNotifications[1].bytes[1]==kValidationProgress);
  ack(target,1);
  if(scenario==3){
    CHECK(target.cancel(2) && cancelled==1 && target.transfer==TransferState::SendingCancel);
    return 0;
  }
  if(scenario==4){
    target.abortTransfer();
    CHECK(cancelled==1 && target.transfer==TransferState::Idle);
    return 0;
  }
  if(scenario==8){
    target.takeProgress();
    target.service(1001);
    CHECK(!target.messageReady && mockNotifyCalls==2 && !target.takeProgress());
    return 0;
  }
  validationServiceProgress=true;target.service(1001);
  CHECK(target.messageType==kValidationProgress && target.awaitingAck);
  ack(target,2);
  validationDone=true;validationFailed=scenario==2;
  target.service(1002);
  if(scenario==2){
    CHECK(target.transfer==TransferState::SendingError && target.messageType==kError);
    CHECK(read16(target.message)==kManifestFailure && cancelled==1);
    target.transmit(1003);
    CHECK(read32(mockNotifications[3].bytes+4)==2);
  }else{
    CHECK(target.transfer==TransferState::Validating && target.messageType==kValidationComplete);
    CHECK(read32(target.message)==12 && read32(target.message+4)==7);
    CHECK(mockNotifications[3].bytes[1]==kValidationComplete);
    ack(target,3);
    CHECK(target.transfer==TransferState::Idle);
    if(scenario==6){
      target.speedAvailable=true;
      const uint8_t speedMode[]={3,4};request(target,0x05,3,speedMode,sizeof(speedMode));
      target.transmit(1003);ack(target,4);
      CHECK(target.speedStatus==2);
    }
    request(target,0x02,3,fields,sizeof(fields));
    CHECK(target.transfer==TransferState::Downloading && exporterOwned);
    CHECK(target.speedActive==bool(scenario==6));
  }
  return 0;
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
  uint8_t cancelBytes[6]{kProtocolVersion,0x04};
  write16(cancelBytes+2,2);write16(cancelBytes+4,crc16(cancelBytes,4));
  onControlWrite(0,&control,cancelBytes,sizeof(cancelBytes));
  target.service(110);
  CHECK(cacheServiceCalls==1 && cacheServiceBudget==16);
  CHECK(target.transfer==TransferState::SendingCancel && target.awaitingAck && finished==1);
  ack(target,0);
  CHECK(target.transfer==TransferState::SendingCancel && target.awaitingAck && !released);
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
  CHECK(cacheServiceCalls==1 && cacheServiceBudget==16);
  CHECK(mockNotifyCalls==1 && target.transfer==TransferState::SendingError);
  CHECK(mockNotifications[0].bytes[1]==kError &&
    read16(mockNotifications[0].bytes+13)==(failed?kManifestFailure:kManifestNotReady));
  ack(target,0);CHECK(target.transfer==TransferState::Idle);
  listAvailable=true;listPending=false;request(target,0x01,3);target.service(110);
  CHECK(mockNotifyCalls==2 && mockNotifications[1].bytes[1]==kListBegin);
  CHECK(read16(mockNotifications[1].bytes+2)==3);
  return 0;
}

extern "C" unsigned performanceTransportCase(unsigned largeMtu){
  resetMocks();mockMtu=largeMtu?247:23;mockNotifyDurationUs=120;mockTime=100;
  Adapter target;beginMessage(target);target.service(millis());
  auto& s=BlePerformance::counters();
  CHECK(s.mtu==mockMtu && s.fragments==1 && !s.notifyFailures);
  CHECK(s.notifyTotalUs==120 && s.notifyMaxUs==120);
  delayMicroseconds(400);ack(target,0);
  CHECK(s.acks==1 && s.ackSamples==1 && s.ackTotalUs==400 && s.ackMinUs==400 && s.ackMaxUs==400);
  const unsigned firstPayload=largeMtu?229:5;
  CHECK(s.ackedPayloadBytes==firstPayload);
  ack(target,0);CHECK(s.acks==1 && s.duplicateAcks==1);
  mockTime+=10;mockNotifyAccepted=false;target.transmit(millis());
  CHECK(s.notifyFailures==1 && s.fragments==1 && !s.retransmissions);
  mockTime+=99;target.transmit(millis());CHECK(s.notifyFailures==1);
  ++mockTime;mockNotifyAccepted=true;target.transmit(millis());
  CHECK(s.fragments==2 && !s.retransmissions);
  mockTime+=1000;target.transmit(millis());
  CHECK(s.fragments==3 && s.retransmissions==1 && s.ackTimeouts==1);
  delayMicroseconds(80);ack(target,3);
  CHECK(s.acks==2 && s.ackSamples==1 && s.retransmittedAcks==1 && s.ackTotalUs==400);
  uint8_t bytes[BlePerformance::SnapshotSize];BlePerformance::snapshot(bytes);
  CHECK(read16(bytes+12)==mockMtu && read16(bytes+14)==(largeMtu?244:20));
  CHECK(read16(bytes+16)==(largeMtu?229:5) && read32(bytes+48)==3);
  CHECK(read32(bytes+52)==2 && read32(bytes+56)==1 && read32(bytes+60)==1);
  return 0;
}

extern "C" unsigned performanceListCase(unsigned){
  resetMocks();Adapter target;mockTime=100;target.service(millis());
  listAvailable=false;listPending=true;mockTime=120;
  request(target,0x01,2);target.service(millis());
  ack(target,0);CHECK(BlePerformance::counters().listRequests==1);
  mockTime=2120;listAvailable=true;listPending=false;
  uint8_t bytes[6]{kProtocolVersion,0x01};write16(bytes+2,3);
  write16(bytes+4,crc16(bytes,4));
  onControlWrite(0,&control,bytes,sizeof(bytes));
  mockTime=2123;target.processControl();
  mockTime=2127;target.service(millis());
  auto& s=BlePerformance::counters();
  CHECK(s.listRequests==2 && s.listToBeginUs==7000);
  CHECK(s.firstListToBeginMs==2007 && s.connectionToListMs==2027 && s.haveListBegin);
  target.transmit(3127);
  CHECK(s.listToBeginUs==7000 && s.firstListToBeginMs==2007);
  return 0;
}

extern "C" unsigned performanceReadCase(unsigned){
  resetMocks();Adapter target;target.performanceAvailable=true;
  auto& s=BlePerformance::counters();s.fragments=4;
  ble_gatts_evt_read_t request{};onPerformanceRead(0,&performance,&request);
  CHECK(!mockReadReplies);target.service(millis());
  CHECK(cacheServiceCalls==1 && cacheServiceBudget==16);
  CHECK(mockReadReplies==1 && mockReadStatus==BLE_GATT_STATUS_SUCCESS);
  CHECK(!memcmp(mockReadValue,"TPD1",4) && read32(mockReadValue+48)==4);
  uint8_t first[BlePerformance::SnapshotSize];memcpy(first,mockReadValue,sizeof(first));
  s.fragments=99;mockTime=500;
  request.offset=22;onPerformanceRead(0,&performance,&request);target.service(millis());
  CHECK(mockReadReplies==2 && !memcmp(first,mockReadValue,sizeof(first)));
  request.offset=0;onPerformanceRead(0,&performance,&request);target.service(millis());
  CHECK(read32(mockReadValue+48)==99 && read32(mockReadValue+168)==500);
  request.offset=BlePerformance::SnapshotSize+1;
  onPerformanceRead(0,&performance,&request);target.service(millis());
  CHECK(mockReadStatus==BLE_GATT_STATUS_ATTERR_INVALID_OFFSET);
  request.offset=0;onPerformanceRead(0,&performance,&request);
  onPerformanceRead(0,&performance,&request);
  CHECK(mockReadStatus==BLE_GATT_STATUS_ATTERR_UNLIKELY_ERROR);
  target.service(millis());CHECK(mockReadStatus==BLE_GATT_STATUS_SUCCESS);
  CHECK(!cancelled && !mockNotifyCalls && target.transfer==TransferState::Idle);
  return 0;
}

extern "C" unsigned performanceBoundsCase(unsigned){
  resetMocks();mockTime=4294967;mockSubUs=280;
  {
    BlePerformance::CacheStep step;
    BlePerformance::FlashRead read(true,64);
    delayMicroseconds(32);
  }
  auto& s=BlePerformance::counters();
  CHECK(s.readCalls==1 && s.readBytes==64 && s.readTotalUs==32 && s.readMaxUs==32);
  CHECK(s.cacheSteps==1 && s.cacheServiceUs==32 && s.cacheReadUs==32 && !s.cacheStepActive);
  s.notifyFailures=UINT32_MAX;s.notifyTotalUs=~uint64_t(0)-1;
  BlePerformance::notification(false,false,20,5,4);
  CHECK(s.notifyFailures==UINT32_MAX && s.notifyTotalUs==~uint64_t(0) && s.saturated);
  uint8_t bytes[BlePerformance::SnapshotSize];BlePerformance::snapshot(bytes);
  CHECK(read32(bytes+172)==1);
  return 0;
}

extern "C" unsigned linkDiagnosticsCase(unsigned unavailable){
  resetMocks();Adapter target;target.linkPerformanceAvailable=true;
  mockConnectionAvailable=!unavailable;
  ble_gatts_evt_read_t request{};request.offset=1;onLinkRead(0,&linkPerformance,&request);
  target.service(millis());CHECK(mockReadStatus==BLE_GATT_STATUS_ATTERR_INVALID_OFFSET);
  request.offset=0;onLinkRead(0,&linkPerformance,&request);
  target.service(millis());
  CHECK(mockReadReplies==2 && mockReadStatus==BLE_GATT_STATUS_SUCCESS);
  CHECK(!memcmp(mockReadValue,"TPL1",4) && read16(mockReadValue+4)==1 &&
    read16(mockReadValue+6)==BlePerformance::LinkSnapshotSize);
  CHECK(mockReadValue[8]==(unavailable?0:1) && mockReadValue[9]==1);
  CHECK(read16(mockReadValue+10)==(unavailable?0:27));
  CHECK(read16(mockReadValue+12)==(unavailable?0:24) && read16(mockReadValue+14)==0);
  uint8_t first[BlePerformance::LinkSnapshotSize];memcpy(first,mockReadValue,sizeof(first));
  mockConnectionAvailable=true;mockPhy=2;mockDataLength=251;mockInterval=12;
  request.offset=8;onLinkRead(0,&linkPerformance,&request);target.service(millis());
  CHECK(!memcmp(first,mockReadValue,sizeof(first)));
  request.offset=0;onLinkRead(0,&linkPerformance,&request);target.service(millis());
  CHECK(mockReadValue[8]==2 && read16(mockReadValue+10)==251 && read16(mockReadValue+12)==12);
  request.offset=BlePerformance::LinkSnapshotSize+1;
  onLinkRead(0,&linkPerformance,&request);target.service(millis());
  CHECK(mockReadStatus==BLE_GATT_STATUS_ATTERR_INVALID_OFFSET);
  CHECK(!cancelled && !mockNotifyCalls && target.transfer==TransferState::Idle);
  return 0;
}
