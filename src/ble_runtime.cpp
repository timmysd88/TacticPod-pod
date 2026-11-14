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
#include "ble_high_speed_window.h"
#include "ble_performance.h"

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
constexpr const char* kPerformanceUuid = "b9730007-7d2a-4f91-a380-8b52c029dc61";
constexpr const char* kLinkPerformanceUuid = "b9730008-7d2a-4f91-a380-8b52c029dc61";
constexpr const char* kSpeedCapabilityUuid = "b9730009-7d2a-4f91-a380-8b52c029dc61";
constexpr const char* kSpeedAckUuid = "b973000a-7d2a-4f91-a380-8b52c029dc61";
BLEService gattService(kServiceUuid);
BLECharacteristic activity(kActivityUuid);
BLECharacteristic battery(kBatteryUuid);
BLECharacteristic control(kControlUuid);
BLECharacteristic data(kDataUuid);
BLECharacteristic protocolVersion(kProtocolUuid);
BLECharacteristic performance(kPerformanceUuid);
BLECharacteristic linkPerformance(kLinkPerformanceUuid);
BLECharacteristic speedCapability(kSpeedCapabilityUuid);
BLECharacteristic speedAck(kSpeedAckUuid);
BLEDis deviceInfo;
std::atomic<uint32_t> events{0};
constexpr uint32_t kActivity = 1, kDisconnected = 2, kInvalid = 4, kPairing = 8;
bool started = false, failed = false;
char podId[17];
constexpr uint8_t kProtocolVersion = BleDownloadProtocol::Version;
constexpr unsigned kMaxFrameSize = BleDownloadProtocol::MaxFrameSize;
constexpr bool kSpeedEnabled = true;
enum class TransferState : uint8_t { Idle, Scanning, Listing, Downloading, SendingEnd, SendingError, SendingCancel, Selecting };
enum MessageType : uint8_t {
  kListBegin = 0x81, kSessionEntry = 0x82, kListEnd = 0x83,
  kTransportPacket = 0x84, kTransferEnd = 0x85, kCancelled = 0x86, kModeSelected = 0x87, kError = 0xff
};
enum ErrorCode : uint16_t {
  kBadProtocol = 1, kBadCommand = 2, kNoSession = 3,
  kManifestFailure = 4, kAckTimeout = 5, kExportFailure = 6, kManifestNotReady = 7
};
uint8_t controlQueue[16];
uint8_t controlLength = 0;
uint32_t controlReceivedUs=0,controlReceivedMs=0;
std::atomic<bool> controlReady{false};
std::atomic<bool> performanceReadReady{false};
uint16_t performanceReadConnection=0,performanceReadOffset=0;
std::atomic<bool> linkReadReady{false};
uint16_t linkReadConnection=0,linkReadOffset=0;
std::atomic<bool> speedReadReady{false},speedAckOverflow{false};
std::atomic<uint32_t> speedAckHead{0},speedAckTail{0};
std::atomic<bool> notificationCredit{true};
uint16_t speedReadConnection=0,speedReadOffset=0;
constexpr unsigned kSpeedAckQueueSize=4;
uint8_t speedAckQueue[kSpeedAckQueueSize][BleHighSpeed::AckSize]{};

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
  controlReceivedUs=micros();controlReceivedMs=millis();
  controlReady.store(true,std::memory_order_release);
}
void onSpeedAck(uint16_t,BLECharacteristic*,uint8_t* bytes,uint16_t length){
  const uint32_t head=speedAckHead.load(std::memory_order_relaxed);
  if(length!=BleHighSpeed::AckSize ||
     uint32_t(head-speedAckTail.load(std::memory_order_acquire))>=kSpeedAckQueueSize){
    speedAckOverflow.store(true,std::memory_order_release);return;
  }
  memcpy(speedAckQueue[head%kSpeedAckQueueSize],bytes,length);
  speedAckHead.store(head+1,std::memory_order_release);
}
void onSpeedRead(uint16_t connection,BLECharacteristic*,ble_gatts_evt_read_t* request){
  if(speedReadReady.load(std::memory_order_acquire)){
    ble_gatts_rw_authorize_reply_params_t reply{};
    reply.type=BLE_GATTS_AUTHORIZE_TYPE_READ;
    reply.params.read.gatt_status=BLE_GATT_STATUS_ATTERR_UNLIKELY_ERROR;
    if(sd_ble_gatts_rw_authorize_reply(connection,&reply)!=NRF_SUCCESS)
      Serial.println("BLE high-speed read reply failed.");
    return;
  }
  speedReadConnection=connection;speedReadOffset=request->offset;
  speedReadReady.store(true,std::memory_order_release);
}
void onPerformanceRead(uint16_t connection,BLECharacteristic*,ble_gatts_evt_read_t* request){
  if(performanceReadReady.load(std::memory_order_acquire)){
    ble_gatts_rw_authorize_reply_params_t reply{};
    reply.type=BLE_GATTS_AUTHORIZE_TYPE_READ;
    reply.params.read.gatt_status=BLE_GATT_STATUS_ATTERR_UNLIKELY_ERROR;
    if(sd_ble_gatts_rw_authorize_reply(connection,&reply)!=NRF_SUCCESS)
      Serial.println("BLE performance read reply failed.");
    return;
  }
  performanceReadConnection=connection;performanceReadOffset=request->offset;
  performanceReadReady.store(true,std::memory_order_release);
}
void onLinkRead(uint16_t connection,BLECharacteristic*,ble_gatts_evt_read_t* request){
  if(linkReadReady.load(std::memory_order_acquire)){
    ble_gatts_rw_authorize_reply_params_t reply{};
    reply.type=BLE_GATTS_AUTHORIZE_TYPE_READ;
    reply.params.read.gatt_status=BLE_GATT_STATUS_ATTERR_UNLIKELY_ERROR;
    if(sd_ble_gatts_rw_authorize_reply(connection,&reply)!=NRF_SUCCESS)
      Serial.println("BLE link diagnostics read reply failed.");
    return;
  }
  linkReadConnection=connection;linkReadOffset=request->offset;
  linkReadReady.store(true,std::memory_order_release);
}
void onEvent(ble_evt_t* event) {
  if(event->header.evt_id==BLE_GATTS_EVT_HVN_TX_COMPLETE)
    notificationCredit.store(true,std::memory_order_release);
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
  bool performanceAvailable=false,performanceSnapshotValid=false;
  bool linkPerformanceAvailable=false,linkSnapshotValid=false;
  uint32_t lastNotifyUs=0;
  uint8_t performanceSnapshot[BlePerformance::SnapshotSize]{};
  uint8_t linkSnapshot[BlePerformance::LinkSnapshotSize]{};
  BleHighSpeed::SendWindow speedWindow{0};
  uint8_t speedBuffer[kMaxFrameSize-BleHighSpeed::Overhead]{},speedSnapshot[16]{};
  unsigned speedBuffered=0,speedPacketOffset=0,speedCapacity=0;
  uint16_t speedId=0,lastSpeedId=0;
  uint8_t speedStatus=0;
  bool speedAvailable=false,speedActive=false,speedSnapshotValid=false;
  bool creditWaiting=false;
  uint32_t creditWaitAt=0;

  void serviceSpeedRead(){
    if(!speedAvailable || !speedReadReady.load(std::memory_order_acquire))return;
    const uint16_t handle=speedReadConnection,offset=speedReadOffset;
    speedReadReady.store(false,std::memory_order_release);
    ble_gatts_rw_authorize_reply_params_t reply{};
    reply.type=BLE_GATTS_AUTHORIZE_TYPE_READ;
    if(offset>sizeof(speedSnapshot) || (offset && !speedSnapshotValid)){
      reply.params.read.gatt_status=BLE_GATT_STATUS_ATTERR_INVALID_OFFSET;
    }else{
      if(!offset){
        memset(speedSnapshot,0,sizeof(speedSnapshot));
        memcpy(speedSnapshot,"TPH1",4);speedSnapshot[4]=3;speedSnapshot[5]=4;
        speedSnapshot[6]=speedStatus;write16(speedSnapshot+8,speedId);
        write16(speedSnapshot+10,kMaxFrameSize);
        write16(speedSnapshot+14,crc16(speedSnapshot,14));
        speedSnapshotValid=true;
      }
      reply.params.read.gatt_status=BLE_GATT_STATUS_SUCCESS;
      reply.params.read.update=1;reply.params.read.offset=offset;
      reply.params.read.len=sizeof(speedSnapshot)-offset;reply.params.read.p_data=speedSnapshot+offset;
    }
    if(sd_ble_gatts_rw_authorize_reply(handle,&reply)!=NRF_SUCCESS)
      Serial.println("BLE high-speed read reply failed.");
  }

  void servicePerformanceRead(){
    if(!performanceAvailable || !performanceReadReady.load(std::memory_order_acquire))return;
    const uint16_t connection=performanceReadConnection,offset=performanceReadOffset;
    performanceReadReady.store(false,std::memory_order_release);
    ble_gatts_rw_authorize_reply_params_t reply{};
    reply.type=BLE_GATTS_AUTHORIZE_TYPE_READ;
    if(offset>sizeof(performanceSnapshot) || (offset && !performanceSnapshotValid)){
      reply.params.read.gatt_status=BLE_GATT_STATUS_ATTERR_INVALID_OFFSET;
    }else{
      // Freeze one foreground snapshot across all fragments of an ATT long read.
      if(!offset){BlePerformance::snapshot(performanceSnapshot);performanceSnapshotValid=true;}
      reply.params.read.gatt_status=BLE_GATT_STATUS_SUCCESS;
      reply.params.read.update=1;reply.params.read.offset=offset;
      reply.params.read.len=sizeof(performanceSnapshot)-offset;
      reply.params.read.p_data=performanceSnapshot+offset;
    }
    if(sd_ble_gatts_rw_authorize_reply(connection,&reply)!=NRF_SUCCESS)
      Serial.println("BLE performance read reply failed.");
  }
  void serviceLinkRead(){
    if(!linkPerformanceAvailable || !linkReadReady.load(std::memory_order_acquire))return;
    const uint16_t handle=linkReadConnection,offset=linkReadOffset;
    linkReadReady.store(false,std::memory_order_release);
    ble_gatts_rw_authorize_reply_params_t reply{};
    reply.type=BLE_GATTS_AUTHORIZE_TYPE_READ;
    if(offset>sizeof(linkSnapshot) || (offset && !linkSnapshotValid)){
      reply.params.read.gatt_status=BLE_GATT_STATUS_ATTERR_INVALID_OFFSET;
    }else{
      if(!offset){
        BLEConnection* connection=Bluefruit.Connection(handle);
        BlePerformance::linkSnapshot(linkSnapshot,connection?connection->getPHY():0,
          connection?connection->getDataLength():0,connection?connection->getConnectionInterval():0);
        linkSnapshotValid=true;
      }
      reply.params.read.gatt_status=BLE_GATT_STATUS_SUCCESS;
      reply.params.read.update=1;reply.params.read.offset=offset;
      reply.params.read.len=sizeof(linkSnapshot)-offset;reply.params.read.p_data=linkSnapshot+offset;
    }
    if(sd_ble_gatts_rw_authorize_reply(handle,&reply)!=NRF_SUCCESS)
      Serial.println("BLE link diagnostics read reply failed.");
  }

  bool begin() {
    Bluefruit.autoConnLed(false);
    // Keep the connection event and notification queues at their conservative defaults.
    started = true; // A partially initialized stack still needs a reset on failure.
    Bluefruit.configPrphConn(BLE_GATT_ATT_MTU_MAX, BLE_GAP_EVENT_LENGTH_MIN,
                            BlePerformance::NotificationQueueCapacity, 1);
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
    performance.setProperties(CHR_PROPS_READ);
    performance.setPermission(SECMODE_OPEN,SECMODE_NO_ACCESS);
    performance.setFixedLen(BlePerformance::SnapshotSize);
    performance.setReadAuthorizeCallback(onPerformanceRead,false);
    const err_t performanceError=performance.begin();
    performanceAvailable=performanceError==ERROR_NONE;
    if(!performanceAvailable){
      Serial.print("BLE performance diagnostics unavailable: GATT error=");
      Serial.println(static_cast<unsigned long>(performanceError));
    }
    linkPerformance.setProperties(CHR_PROPS_READ);
    linkPerformance.setPermission(SECMODE_OPEN,SECMODE_NO_ACCESS);
    linkPerformance.setFixedLen(BlePerformance::LinkSnapshotSize);
    linkPerformance.setReadAuthorizeCallback(onLinkRead,false);
    const err_t linkPerformanceError=linkPerformance.begin();
    linkPerformanceAvailable=linkPerformanceError==ERROR_NONE;
    if(!linkPerformanceAvailable){
      Serial.print("BLE link diagnostics unavailable: GATT error=");
      Serial.println(static_cast<unsigned long>(linkPerformanceError));
    }
    speedAck.setProperties(CHR_PROPS_WRITE);
    speedAck.setPermission(SECMODE_NO_ACCESS,SECMODE_OPEN);
    speedAck.setFixedLen(BleHighSpeed::AckSize);
    speedAck.setWriteCallback(onSpeedAck,false);
    const err_t speedAckError=kSpeedEnabled?speedAck.begin():-1;
    if(speedAckError==ERROR_NONE){
      speedCapability.setProperties(CHR_PROPS_READ);
      speedCapability.setPermission(SECMODE_OPEN,SECMODE_NO_ACCESS);
      speedCapability.setFixedLen(sizeof(speedSnapshot));
      speedCapability.setReadAuthorizeCallback(onSpeedRead,false);
      speedAvailable=speedCapability.begin()==ERROR_NONE;
    }
    if(!speedAvailable)Serial.println("BLE high-speed capability unavailable; protocol v2 remains available.");
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
    BLEConnection* connection=Bluefruit.Connection(Bluefruit.connHandle());
    if(connection)BlePerformance::observeConnection(connection->getMtu());
    processControl();
    processSpeedAck();
    if(Tcb3Session::bleService(16))progressPending=true;
    if(transfer==TransferState::Scanning){
      if(Tcb3Session::bleListDone()){
        transfer=TransferState::Listing;listStage=0;listIndex=0;messageSequence=0;
        listCount=Tcb3Session::bleListFailed()?0:Tcb3Session::bleSessionCount();
      }
    }else if(transfer==TransferState::Downloading){
      if(Tcb3Session::bleTransferFailed()){queueError(kExportFailure);return;}
    }
    if(speedActive && transfer==TransferState::Downloading)serviceSpeed(now);
    else {prepareMessage();transmit(now);}
    servicePerformanceRead();
    serviceLinkRead();
    serviceSpeedRead();
  }
  bool transferActive() const { return transfer!=TransferState::Idle; }
  bool takeProgress() { bool value=progressPending;progressPending=false;return value; }
  void abortTransfer() {
    Tcb3Session::bleCancel();
    transfer=TransferState::Idle;messageReady=frameReady=awaitingAck=false;
    if(speedActive || speedStatus==1 || speedStatus==2)speedStatus=5;
    speedActive=false;speedBuffered=speedPacketOffset=0;creditWaiting=false;
    speedAckTail.store(speedAckHead.load(std::memory_order_acquire),std::memory_order_release);
    speedAckOverflow.store(false);
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
    if(speedStatus==2)speedStatus=5;
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
    speedActive=speedAvailable && speedStatus==2 && speedId==id;
    if(speedStatus==2 && !speedActive)speedStatus=5;
    if(!Tcb3Session::bleBeginSession(session,epoch)){queueError(kNoSession);return false;}
    sessionId=session;sessionEpoch=epoch;totalBytes=0;messageSequence=speedActive?1:0;
    if(speedActive){
      speedStatus=3;speedWindow.reset(id);speedBuffered=speedPacketOffset=0;speedCapacity=0;creditWaiting=false;
    }
    haveLastAck=false;transfer=TransferState::Downloading;return true;
  }
  void processControl() {
    if(!controlReady.load(std::memory_order_acquire))return;
    uint8_t bytes[sizeof(controlQueue)];const uint8_t length=controlLength;
    const uint32_t receivedUs=controlReceivedUs,receivedMs=controlReceivedMs;
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
      BlePerformance::listRequested(receivedUs,receivedMs);
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
    if(opcode==0x05 && length==8 && bytes[4]==3 && bytes[5]==4 && speedAvailable){
      if(transfer!=TransferState::Idle || speedStatus==2 || id<=lastSpeedId){
        events.fetch_or(kInvalid);return;
      }
      speedId=lastSpeedId=id;speedStatus=1;transferId=id;
      transfer=TransferState::Selecting;messageSequence=0;haveLastAck=false;
      const uint8_t selected[]={3,4};setMessage(kModeSelected,selected,sizeof(selected));
      events.fetch_or(kActivity);return;
    }
    if(transfer==TransferState::Idle){transferId=id;queueError(kBadCommand);}
    events.fetch_or(kInvalid);
  }
  void acceptAck(uint16_t id,uint32_t sequence,uint16_t offset,uint16_t checksum) {
    if(id==transferId && awaitingAck && sequence==messageSequence && offset==frameOffset && checksum==frameCrc){
      BlePerformance::acknowledged(framePayloadLength,frameAttempts>1,uint32_t(micros()-lastNotifyUs));
      lastAckSequence=sequence;lastAckOffset=offset;lastAckCrc=checksum;haveLastAck=true;
      awaitingAck=false;frameReady=false;messageOffset+=framePayloadLength;
      progressPending=true;events.fetch_or(kActivity);
      if(messageOffset==messageLength)completeMessage();
      return;
    }
    if(haveLastAck && id==transferId && sequence==lastAckSequence &&
       offset==lastAckOffset && checksum==lastAckCrc){
      BlePerformance::add(BlePerformance::counters().duplicateAcks,1);return;
    }
    events.fetch_or(kInvalid);
  }
  bool cancel(uint16_t id) {
    if(id!=transferId || (transfer==TransferState::Idle && speedStatus!=2))return false;
    if(transfer==TransferState::Scanning || transfer==TransferState::Listing)Tcb3Session::bleCancel();
    else if(transfer==TransferState::Downloading || transfer==TransferState::SendingEnd)
      Tcb3Session::bleFinishSession();
    if(speedActive || speedStatus==1 || speedStatus==2)speedStatus=5;
    speedActive=false;speedBuffered=speedPacketOffset=0;
    messageReady=frameReady=awaitingAck=false;transfer=TransferState::SendingCancel;
    haveLastAck=false;
    const uint8_t payload[2]={0,0};setMessage(kCancelled,payload,sizeof(payload));
    return true;
  }
  void queueError(uint16_t code) {
    const bool replacingTransfer=transfer!=TransferState::Idle;
    const bool speedTransfer=speedActive || transfer==TransferState::Selecting;
    if(replacingTransfer)Tcb3Session::bleCancel();
    if(speedActive || speedStatus==1 || speedStatus==2)speedStatus=5;
    speedActive=false;speedBuffered=speedPacketOffset=0;
    transfer=TransferState::SendingError;
    if(replacingTransfer && !speedTransfer)++messageSequence;
    else if(!replacingTransfer)messageSequence=0;
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
    if(transfer==TransferState::Selecting){
      speedStatus=2;transfer=TransferState::Idle;return;
    }else if(transfer==TransferState::Listing){
      if(listStage==0)listStage=1;
      else if(listStage==1){++listIndex;if(listIndex>=listCount)listStage=2;}
      else{Tcb3Session::bleFinishList();transfer=TransferState::Idle;return;}
    }else if(transfer==TransferState::Downloading && lastMessageWasExportEnd){
      if(!Tcb3Session::bleTransferDone()){queueError(kExportFailure);return;}
      transfer=TransferState::SendingEnd;uint8_t payload[12];
      write32(payload,sessionId);write32(payload+4,sessionEpoch);write32(payload+8,totalBytes);
      setMessage(kTransferEnd,payload,sizeof(payload));
    }else if(transfer==TransferState::SendingEnd){
      if(speedActive){speedStatus=4;speedActive=false;}
      Tcb3Session::bleFinishSession();transfer=TransferState::Idle;return;
    }else if(transfer==TransferState::SendingError || transfer==TransferState::SendingCancel){
      transfer=TransferState::Idle;return;
    }
  }
  void processSpeedAck(){
    const bool overflow=speedAckOverflow.exchange(false,std::memory_order_acq_rel);
    if(overflow){
      speedAckTail.store(speedAckHead.load(std::memory_order_acquire),std::memory_order_release);
      Serial.println("BLE high-speed ACK queue overflow.");
      if(speedActive && transfer==TransferState::Downloading)queueError(kBadProtocol);
      return;
    }
    const uint32_t tail=speedAckTail.load(std::memory_order_relaxed);
    if(tail==speedAckHead.load(std::memory_order_acquire))return;
    uint8_t bytes[BleHighSpeed::AckSize];
    memcpy(bytes,speedAckQueue[tail%kSpeedAckQueueSize],sizeof(bytes));
    speedAckTail.store(tail+1,std::memory_order_release);
    if(!speedActive || transfer!=TransferState::Downloading || read16(bytes+2)!=speedId){
      events.fetch_or(kInvalid);return;
    }
    const auto result=speedWindow.acknowledge(bytes,sizeof(bytes));
    if(result==BleHighSpeed::SendWindow::Result::Invalid){queueError(kBadProtocol);return;}
    if(result==BleHighSpeed::SendWindow::Result::Ok){
      progressPending=true;events.fetch_or(kActivity);
    }
  }
  void serviceSpeed(uint32_t now){
    using Result=BleHighSpeed::SendWindow::Result;
    BLEConnection* connection=Bluefruit.Connection(Bluefruit.connHandle());
    if(!connection || connection->getMtu()<23){queueError(kBadProtocol);return;}
    unsigned capacity=connection->getMtu()-3;
    if(capacity>kMaxFrameSize)capacity=kMaxFrameSize;
    if(!speedCapacity)speedCapacity=capacity;
    if(capacity<speedCapacity){queueError(kBadProtocol);return;}
    if(!speedWindow.finalQueued() && speedWindow.next()-speedWindow.base()<BleHighSpeed::WindowSize){
      unsigned length=0;const uint8_t* packet=Tcb3Session::blePacket(length);
      if(packet){
        if(length<24 || length>sizeof(message) || speedPacketOffset>=length ||
           memcmp(packet,"TPX3",4) || (packet[5]!=1 && packet[5]!=2 && packet[5]!=4)){
          queueError(kExportFailure);return;
        }
        const unsigned room=speedCapacity-BleHighSpeed::Overhead-speedBuffered;
        const unsigned remaining=length-speedPacketOffset;
        const unsigned amount=remaining<room?remaining:room;
        memcpy(speedBuffer+speedBuffered,packet+speedPacketOffset,amount);
        speedBuffered+=amount;speedPacketOffset+=amount;totalBytes+=amount;
        const bool ended=speedPacketOffset==length && packet[5]==4;
        if(speedPacketOffset==length){Tcb3Session::bleReleasePacket();speedPacketOffset=0;}
        if(ended && !Tcb3Session::bleTransferDone()){queueError(kExportFailure);return;}
        if(ended || speedBuffered==speedCapacity-BleHighSpeed::Overhead){
          if(speedWindow.append(speedBuffer,speedBuffered,speedCapacity,ended)!=Result::Ok){
            queueError(kBadProtocol);return;
          }
          speedBuffered=0;
        }
      }
    }
    if(speedWindow.finalQueued() && speedWindow.base()==speedWindow.next()){
      transfer=TransferState::SendingEnd;uint8_t payload[12];
      write32(payload,sessionId);write32(payload+4,sessionEpoch);write32(payload+8,totalBytes);
      setMessage(kTransferEnd,payload,sizeof(payload));return;
    }
    const BleHighSpeed::SendWindow::Frame* candidate=nullptr;
    const Result result=speedWindow.candidate(now,candidate);
    if(result==Result::RetryExhausted || result==Result::QueueExhausted){queueError(kAckTimeout);return;}
    if(result!=Result::Ok || !data.notifyEnabled()){creditWaiting=false;return;}
    if(!notificationCredit.exchange(false,std::memory_order_acq_rel)){
      if(!creditWaiting){creditWaiting=true;creditWaitAt=now;}
      else if(uint32_t(now-creditWaitAt)>=5000)queueError(kAckTimeout);
      return;
    }
    creditWaiting=false;
    const uint32_t start=micros();
    const bool accepted=data.notify(candidate->bytes,candidate->length);
    const uint32_t elapsed=uint32_t(micros()-start);
    BlePerformance::notification(accepted,candidate->attempts>0,candidate->length,
                                candidate->bytes[8],elapsed);
    if(!accepted)notificationCredit.store(true,std::memory_order_release);
    if(speedWindow.submitted(candidate->sequence,now,accepted)!=Result::Ok)queueError(kBadProtocol);
  }
  void transmit(uint32_t now) {
    if(!messageReady || !data.notifyEnabled())return;
    if(awaitingAck){
      if(uint32_t(now-lastSentAt)<1000)return;
      BlePerformance::add(BlePerformance::counters().ackTimeouts,1);
      if(frameAttempts>=5){transmissionFailed();return;}
      awaitingAck=false;
    }
    if(!frameReady)buildFrame();
    if(queueFailures && uint32_t(now-lastAttemptAt)<100)return;
    lastAttemptAt=now;
    const uint32_t notifyStartedUs=micros();
    notificationCredit.store(false,std::memory_order_release);
    const bool accepted=data.notify(frame,frameLength);
    if(!accepted)notificationCredit.store(true,std::memory_order_release);
    const uint32_t notifyFinishedUs=micros();
    BlePerformance::notification(accepted,frameAttempts>0,frameLength,framePayloadLength,
                                uint32_t(notifyFinishedUs-notifyStartedUs));
    if(accepted){
      if(messageType==kListBegin && frameOffset==0 && !frameAttempts){
        BlePerformance::listBegin();
      }
      lastNotifyUs=notifyFinishedUs;
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
