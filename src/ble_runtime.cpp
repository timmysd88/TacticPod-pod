#if TACTICPOD_BLE
#include <Arduino.h>
#include <bluefruit.h>
#include <InternalFileSystem.h>
#include <flash/flash_nrf5x.h>
#include <atomic>
#include "battery_runtime.h"
#include "ble_phase1.h"
#include "ble_runtime.h"
#include "tcb3_session.h"
#include "ble_download_protocol.h"

#if !TACTICPOD_TCB3_FLASH_ENABLED || !TACTICPOD_REUSABLE
#error TACTICPOD_BLE requires the reusable TCB3 flash profile
#endif

namespace {
constexpr uint32_t kResetMarker = 0x54424C45;
struct ResetMarker { uint32_t magic, inverse; };
volatile ResetMarker resetMarker __attribute__((section(".ble_reset"), used));
constexpr const char* kServiceUuid = "b9730001-7d2a-4f91-a380-8b52c029dc61";
constexpr const char* kActivityUuid = "b9730002-7d2a-4f91-a380-8b52c029dc61";
constexpr const char* kBatteryUuid = "b9730003-7d2a-4f91-a380-8b52c029dc61";
constexpr const char* kControlUuid = "b9730004-7d2a-4f91-a380-8b52c029dc61";
constexpr const char* kDataUuid = "b9730005-7d2a-4f91-a380-8b52c029dc61";
constexpr const char* kProtocolUuid = "b9730006-7d2a-4f91-a380-8b52c029dc61";
BLEService gattService(kServiceUuid);
BLECharacteristic activity(kActivityUuid);
BLECharacteristic battery(kBatteryUuid);
BLECharacteristic control(kControlUuid);
BLECharacteristic data(kDataUuid);
BLECharacteristic protocolVersion(kProtocolUuid);
BLEDis deviceInfo;
std::atomic<uint32_t> events{0};
constexpr uint32_t kActivity = 1, kDisconnected = 2, kInvalid = 4, kPairing = 8;
bool started = false, failed = false;
char podId[17];
constexpr uint8_t kProtocolVersion = BleDownloadProtocol::Version;
constexpr unsigned kMaxFrameSize = BleDownloadProtocol::MaxFrameSize;
enum class TransferState : uint8_t { Idle, Scanning, Listing, Downloading, SendingEnd, SendingError, SendingCancel };
enum MessageType : uint8_t {
  kListBegin = 0x81, kSessionEntry = 0x82, kListEnd = 0x83,
  kTransportPacket = 0x84, kTransferEnd = 0x85, kCancelled = 0x86, kError = 0xff
};
enum ErrorCode : uint16_t {
  kBadProtocol = 1, kBadCommand = 2, kNoSession = 3,
  kManifestFailure = 4, kAckTimeout = 5, kExportFailure = 6, kManifestNotReady = 7
};
uint8_t controlQueue[16];
uint8_t controlLength = 0;
std::atomic<bool> controlReady{false};

using BleDownloadProtocol::crc16;
using BleDownloadProtocol::read16;
using BleDownloadProtocol::read32;
using BleDownloadProtocol::write16;
using BleDownloadProtocol::write32;
using BleDownloadProtocol::write64;

void onDisconnect(uint16_t, uint8_t) { events.fetch_or(kDisconnected); }
void onWrite(uint16_t, BLECharacteristic*, uint8_t* data, uint16_t len) {
  events.fetch_or(len == 1 && data[0] == 1 ? kActivity : kInvalid);
}
void onControlWrite(uint16_t,BLECharacteristic*,uint8_t* bytes,uint16_t length) {
  if(!length || length>sizeof(controlQueue) || controlReady.load(std::memory_order_acquire)){
    events.fetch_or(kInvalid);return;
  }
  memcpy(controlQueue,bytes,length);controlLength=uint8_t(length);
  controlReady.store(true,std::memory_order_release);
}
void onEvent(ble_evt_t* event) {
  if (event->header.evt_id == BLE_GAP_EVT_SEC_PARAMS_REQUEST) {
    // Phase 1 uses open GATT only; terminate unsolicited pairing before it can save bonds.
    events.fetch_or(kPairing);
    if (sd_ble_gap_disconnect(event->evt.gap_evt.conn_handle,
                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION) != NRF_SUCCESS)
      events.fetch_or(kDisconnected);
  }
}

struct Adapter {
  TransferState transfer = TransferState::Idle;
  uint8_t message[280]{},frame[kMaxFrameSize]{};
  uint16_t messageLength=0,messageOffset=0,transferId=0;
  uint8_t messageType=0;
  uint32_t messageSequence=0,lastSentAt=0,lastAttemptAt=0,totalBytes=0;
  uint32_t sessionId=0,sessionEpoch=0;
  uint16_t frameCrc=0,frameOffset=0,framePayloadLength=0;
  uint16_t lastAckCrc=0,lastAckOffset=0;
  uint32_t lastAckSequence=0;
  uint8_t frameLength=0,frameAttempts=0,queueFailures=0;
  unsigned listIndex=0,listCount=0;
  uint8_t listStage=0;
  bool messageReady=false,frameReady=false,awaitingAck=false,haveLastAck=false;
  bool lastMessageWasExportEnd=false,progressPending=false;

  bool begin() {
    Bluefruit.autoConnLed(false);
    // Keep the connection event and notification queues at their conservative defaults.
    started = true; // A partially initialized stack still needs a reset on failure.
    Bluefruit.configPrphConn(BLE_GATT_ATT_MTU_MAX, BLE_GAP_EVENT_LENGTH_MIN, 1, 1);
    if (!Bluefruit.begin(1, 0)) return fail("SoftDevice initialization");
    Bluefruit.setName("TacticPod"); // begin() installs the framework's default name.
    if (!InternalFS.exists("/adafruit/bond_prph") || !InternalFS.exists("/adafruit/bond_cntr"))
      return fail("internal filesystem initialization");
    flash_nrf5x_flush();
    Bluefruit.Periph.setDisconnectCallback(onDisconnect);
    Bluefruit.setEventCallback(onEvent);
    Bluefruit.Advertising.restartOnDisconnect(false);
    snprintf(podId, sizeof(podId), "%08lx%08lx",
             static_cast<unsigned long>(NRF_FICR->DEVICEID[1]),
             static_cast<unsigned long>(NRF_FICR->DEVICEID[0]));
    deviceInfo.setManufacturer("TacticPod");
    deviceInfo.setModel("XIAO nRF52840 Sense");
    deviceInfo.setSerialNum(podId);
    deviceInfo.setFirmwareRev("ble-download-2");
    if (deviceInfo.begin() != ERROR_NONE || gattService.begin() != ERROR_NONE)
      return fail("GATT service initialization");
    activity.setProperties(CHR_PROPS_WRITE);
    activity.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
    activity.setMaxLen(20);
    activity.setWriteCallback(onWrite, false);
    battery.setProperties(CHR_PROPS_READ);
    battery.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    battery.setFixedLen(6);
    control.setProperties(CHR_PROPS_WRITE);
    control.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
    control.setMaxLen(sizeof(controlQueue));
    control.setWriteCallback(onControlWrite,false);
    data.setProperties(CHR_PROPS_NOTIFY);
    // Bluefruit derives CCCD write permission from the notification read permission.
    data.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    data.setMaxLen(kMaxFrameSize);
    protocolVersion.setProperties(CHR_PROPS_READ);
    protocolVersion.setPermission(SECMODE_OPEN,SECMODE_NO_ACCESS);
    protocolVersion.setFixedLen(2);
    const uint8_t version[]={kProtocolVersion,0};
    if (!beginCharacteristic(activity, "activity") || !beginCharacteristic(battery, "battery") ||
        !beginCharacteristic(control, "control") || !beginCharacteristic(data, "data") ||
        !beginCharacteristic(protocolVersion, "protocol"))
      return false;
    if(protocolVersion.write(version,sizeof(version))!=sizeof(version))
      return fail("protocol version characteristic");
    if (!updateBattery()) return false;
    if (!Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE) ||
        !Bluefruit.Advertising.addService(gattService) || !Bluefruit.ScanResponse.addName())
      return fail("advertising payload");
    Bluefruit.Advertising.setIntervalMS(100, 100);
    if (!Bluefruit.Advertising.start(180)) return fail("advertising start");
    Serial.println("BLE advertising; session listing/download protocol v2 ready.");
    return true;
  }
  bool beginCharacteristic(BLECharacteristic& characteristic, const char* name) {
    const err_t error = characteristic.begin();
    if (error == ERROR_NONE) return true;
    Serial.print("BLE GATT characteristic="); Serial.print(name);
    Serial.print(" error="); Serial.println(static_cast<unsigned long>(error));
    return fail("GATT characteristic initialization");
  }
  bool fail(const char* reason) {
    ::failed = true;
    Serial.print("BLE ERROR: "); Serial.println(reason);
    return false;
  }
  bool connected() { return started && Bluefruit.connected(); }
  bool takeActivity() {
    const uint32_t pending = events.exchange(0);
    if (pending & kDisconnected) ::failed = true; // Do not miss a short-lived connection.
    if (pending & kInvalid) Serial.println("BLE ignored malformed activity/control request.");
    if (pending & kPairing) {
      Serial.println("BLE refused pairing: Phase 1 supports unbonded connections only.");
      ::failed = true;
    }
    return (pending & kActivity) != 0;
  }
  bool failed() { return ::failed; }
  void service(uint32_t now) {
    if(!connected())return;
    processControl();
    if(Tcb3Session::bleService(16))progressPending=true;
    if(transfer==TransferState::Scanning){
      if(Tcb3Session::bleListDone()){
        transfer=TransferState::Listing;listStage=0;listIndex=0;messageSequence=0;
        listCount=Tcb3Session::bleListFailed()?0:Tcb3Session::bleSessionCount();
      }
    }else if(transfer==TransferState::Downloading){
      if(Tcb3Session::bleTransferFailed()){queueError(kExportFailure);return;}
    }
    prepareMessage();
    transmit(now);
  }
  bool transferActive() const { return transfer!=TransferState::Idle; }
  bool takeProgress() { bool value=progressPending;progressPending=false;return value; }
  void abortTransfer() {
    Tcb3Session::bleCancel();
    transfer=TransferState::Idle;messageReady=frameReady=awaitingAck=false;
  }
  void stop() {
    abortTransfer();
    if (!started) return;
    if (Bluefruit.Advertising.isRunning() && !Bluefruit.Advertising.stop())
      fail("advertising stop");
    if (Bluefruit.connected() && !Bluefruit.disconnect(Bluefruit.connHandle()))
      fail("disconnect request");
  }
  bool quiescent() {
    return !connected() && (!started || !Bluefruit.Advertising.isRunning()) &&
           NRF_NVMC->READY != 0;
  }
  void reset() {
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, HIGH);
    resetMarker.magic = kResetMarker;
    resetMarker.inverse = ~kResetMarker;
    __DSB();
    NVIC_SystemReset();
  }
  bool updateBattery() {
    BatteryRuntime::Report r;
    BatteryRuntime::capture(r);
    // Version, flags (fresh/trusted/cell/USB), percent (255=unknown), mV little-endian, reserved.
    const uint8_t bytes[] = {
      1, uint8_t((r.fresh ? 1 : 0) | (r.trusted ? 2 : 0) | (r.cell ? 4 : 0) | (r.usb ? 8 : 0)),
      uint8_t(r.estimate ? r.percent : 255), uint8_t(r.mv), uint8_t(r.mv >> 8), 0
    };
    return battery.write(bytes, sizeof(bytes)) == sizeof(bytes) || fail("battery update");
  }
  bool startList(uint16_t id) {
    if(transfer!=TransferState::Idle)return false;
    bool notReady=false;
    if(!Tcb3Session::bleBeginList(&notReady)){
      transferId=id;queueError(notReady?kManifestNotReady:kManifestFailure);return false;
    }
    transferId=id;transfer=TransferState::Scanning;listCount=listIndex=0;listStage=0;
    messageSequence=0;totalBytes=0;haveLastAck=false;return true;
  }
  bool startDownload(uint16_t id,uint32_t session,uint32_t epoch) {
    if(transfer!=TransferState::Idle)return false;
    transferId=id;
    if(!Tcb3Session::bleBeginSession(session,epoch)){queueError(kNoSession);return false;}
    sessionId=session;sessionEpoch=epoch;totalBytes=0;messageSequence=0;
    haveLastAck=false;transfer=TransferState::Downloading;return true;
  }
  void processControl() {
    if(!controlReady.load(std::memory_order_acquire))return;
    uint8_t bytes[sizeof(controlQueue)];const uint8_t length=controlLength;
    memcpy(bytes,controlQueue,length);controlReady.store(false,std::memory_order_release);
    if(length<6){
      events.fetch_or(kInvalid);return;
    }
    const uint8_t opcode=bytes[1];const uint16_t id=read16(bytes+2);
    if(!id){events.fetch_or(kInvalid);return;}
    if(bytes[0]!=kProtocolVersion || read16(bytes+length-2)!=crc16(bytes,length-2)){
      if(transfer==TransferState::Idle){transferId=id;queueError(kBadProtocol);}
      events.fetch_or(kInvalid);return;
    }
    if(opcode==0x01 && length==6){
      if(transfer!=TransferState::Idle){events.fetch_or(kInvalid);return;}
      startList(id);events.fetch_or(kActivity);return;
    }
    if(opcode==0x02 && length==14){
      if(transfer!=TransferState::Idle){events.fetch_or(kInvalid);return;}
      startDownload(id,read32(bytes+4),read32(bytes+8));events.fetch_or(kActivity);return;
    }
    if(opcode==0x03 && length==14){acceptAck(id,read32(bytes+4),read16(bytes+8),read16(bytes+10));return;}
    if(opcode==0x04 && length==6){
      if(cancel(id))events.fetch_or(kActivity);else events.fetch_or(kInvalid);
      return;
    }
    if(transfer==TransferState::Idle){transferId=id;queueError(kBadCommand);}
    events.fetch_or(kInvalid);
  }
  void acceptAck(uint16_t id,uint32_t sequence,uint16_t offset,uint16_t checksum) {
    if(id==transferId && awaitingAck && sequence==messageSequence && offset==frameOffset && checksum==frameCrc){
      lastAckSequence=sequence;lastAckOffset=offset;lastAckCrc=checksum;haveLastAck=true;
      awaitingAck=false;frameReady=false;messageOffset+=framePayloadLength;
      progressPending=true;events.fetch_or(kActivity);
      if(messageOffset==messageLength)completeMessage();
      return;
    }
    if(haveLastAck && id==transferId && sequence==lastAckSequence &&
       offset==lastAckOffset && checksum==lastAckCrc)return;
    events.fetch_or(kInvalid);
  }
  bool cancel(uint16_t id) {
    if(id!=transferId || transfer==TransferState::Idle)return false;
    if(transfer==TransferState::Scanning || transfer==TransferState::Listing)Tcb3Session::bleCancel();
    else if(transfer==TransferState::Downloading || transfer==TransferState::SendingEnd)
      Tcb3Session::bleFinishSession();
    messageReady=frameReady=awaitingAck=false;transfer=TransferState::SendingCancel;
    haveLastAck=false;
    const uint8_t payload[2]={0,0};setMessage(kCancelled,payload,sizeof(payload));
    return true;
  }
  void queueError(uint16_t code) {
    const bool replacingTransfer=transfer!=TransferState::Idle;
    if(replacingTransfer)Tcb3Session::bleCancel();
    transfer=TransferState::SendingError;
    if(replacingTransfer)++messageSequence;else messageSequence=0;
    haveLastAck=false;
    uint8_t payload[2];write16(payload,code);setMessage(kError,payload,sizeof(payload));
  }
  void setMessage(uint8_t type,const uint8_t* bytes,unsigned length) {
    if(length>sizeof(message)){queueError(kBadProtocol);return;}
    messageType=type;messageLength=uint16_t(length);messageOffset=0;
    if(length)memcpy(message,bytes,length);
    messageReady=true;frameReady=awaitingAck=false;frameAttempts=queueFailures=0;
  }
  void prepareMessage() {
    if(messageReady)return;
    if(transfer==TransferState::Listing){
      if(listStage==0){
        uint8_t payload[8]{};
        const uint16_t status=Tcb3Session::bleListFailed()?2:
          Tcb3Session::bleListOverflow()?1:0;
        write16(payload,status);write16(payload+2,uint16_t(listCount));
        write32(payload+4,Tcb3Session::bleIncompleteCount());
        setMessage(kListBegin,payload,sizeof(payload));return;
      }
      if(listStage==1 && listIndex<listCount){
        const Tcb3::ManifestEntry* entry=Tcb3Session::bleSession(listIndex);
        if(!entry){queueError(kManifestFailure);return;}
        serializeEntry(*entry);return;
      }
      if(listStage<=2){
        uint8_t payload[2];write16(payload,uint16_t(listCount));
        listStage=2;setMessage(kListEnd,payload,sizeof(payload));return;
      }
      transfer=TransferState::Idle;return;
    }
    if(transfer==TransferState::Downloading){
      unsigned length=0;const uint8_t* packet=Tcb3Session::blePacket(length);
      if(packet){
        if(length>sizeof(message)){queueError(kExportFailure);return;}
        lastMessageWasExportEnd=length>=6 && packet[5]==4;
        totalBytes+=length;setMessage(kTransportPacket,packet,length);return;
      }
    }
  }
  void serializeEntry(const Tcb3::ManifestEntry& entry) {
    write32(message,entry.sessionId);write32(message+4,entry.resetEpoch);
    write32(message+8,entry.firstAddress);write32(message+12,entry.sectorCount);
    write32(message+16,entry.formatVersion);write32(message+20,entry.flags);
    write32(message+24,entry.footerReason);write64(message+28,entry.startUs);
    write64(message+36,entry.stopUs);write64(message+44,entry.deviceId);
    for(unsigned i=0;i<4;++i){
      write64(message+52+8*i,entry.committed[i]);
      write64(message+84+8*i,entry.lost[i]);
    }
    messageType=kSessionEntry;messageLength=116;messageOffset=0;
    messageReady=true;frameReady=awaitingAck=false;frameAttempts=queueFailures=0;
  }
  void completeMessage() {
    const uint8_t completedType=messageType;
    messageReady=false;frameReady=awaitingAck=false;messageOffset=0;++messageSequence;
    if(completedType==kTransportPacket)Tcb3Session::bleReleasePacket();
    if(transfer==TransferState::Listing){
      if(listStage==0)listStage=1;
      else if(listStage==1){++listIndex;if(listIndex>=listCount)listStage=2;}
      else{Tcb3Session::bleFinishList();transfer=TransferState::Idle;return;}
    }else if(transfer==TransferState::Downloading && lastMessageWasExportEnd){
      if(!Tcb3Session::bleTransferDone()){queueError(kExportFailure);return;}
      transfer=TransferState::SendingEnd;uint8_t payload[12];
      write32(payload,sessionId);write32(payload+4,sessionEpoch);write32(payload+8,totalBytes);
      setMessage(kTransferEnd,payload,sizeof(payload));
    }else if(transfer==TransferState::SendingEnd){
      Tcb3Session::bleFinishSession();transfer=TransferState::Idle;return;
    }else if(transfer==TransferState::SendingError || transfer==TransferState::SendingCancel){
      transfer=TransferState::Idle;return;
    }
  }
  void transmit(uint32_t now) {
    if(!messageReady || !data.notifyEnabled())return;
    if(awaitingAck){
      if(uint32_t(now-lastSentAt)<1000)return;
      if(frameAttempts>=5){transmissionFailed();return;}
      awaitingAck=false;
    }
    if(!frameReady)buildFrame();
    if(queueFailures && uint32_t(now-lastAttemptAt)<100)return;
    lastAttemptAt=now;
    if(data.notify(frame,frameLength)){
      awaitingAck=true;lastSentAt=now;++frameAttempts;queueFailures=0;
    }else if(++queueFailures>=5)transmissionFailed();
  }
  void transmissionFailed() {
    if(transfer==TransferState::SendingError || transfer==TransferState::SendingCancel){
      abortTransfer();return;
    }
    queueError(kAckTimeout);
  }
  void buildFrame() {
    BLEConnection* connection=Bluefruit.Connection(Bluefruit.connHandle());
    unsigned capacity=20;
    if(connection){
      const uint16_t mtu=connection->getMtu();
      if(mtu>3)capacity=mtu-3;
    }
    if(capacity>kMaxFrameSize)capacity=kMaxFrameSize;
    frameLength=uint8_t(BleDownloadProtocol::encodeFrame(
      frame,capacity,messageType,transferId,messageSequence,messageOffset,message,
      messageLength,framePayloadLength,frameCrc));
    if(!frameLength){queueError(kBadProtocol);return;}
    frameOffset=messageOffset;frameReady=true;frameAttempts=queueFailures=0;
  }
};
Adapter adapter;
BlePhase1::Runtime<Adapter> runtime;
uint32_t lastBatteryAt = 0;
}

namespace BleRuntime {
bool active() { return runtime.active(); }
StatusLed::Color ledColor(uint32_t now) { return runtime.ledColor(now); }
void update(uint32_t now, const BleMode::Inputs& in) {
  runtime.update(now, in, adapter);
  if (runtime.state() == BleMode::State::Connected && uint32_t(now - lastBatteryAt) >= 1000) {
    lastBatteryAt = now;
    adapter.updateBattery();
  }
}
bool returnedFromBle() {
  const bool returned = resetMarker.magic == kResetMarker && resetMarker.inverse == ~kResetMarker;
  resetMarker.magic = resetMarker.inverse = 0;
  return returned;
}
}
#endif
