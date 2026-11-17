#pragma once
#include <Arduino.h>

typedef int err_t;
constexpr err_t ERROR_NONE=0,NRF_SUCCESS=0;
constexpr err_t NRF_ERROR_RESOURCES=19,NRF_ERROR_INTERNAL=3;
constexpr unsigned BLE_GATT_HVX_NOTIFICATION=1;
constexpr uint8_t BLE_GAP_PHY_2MBPS=2;
constexpr unsigned BLE_GATT_ATT_MTU_MAX=247,BLE_GAP_EVENT_LENGTH_MIN=6;
constexpr unsigned CHR_PROPS_WRITE=8,CHR_PROPS_READ=2,CHR_PROPS_NOTIFY=16;
constexpr unsigned SECMODE_NO_ACCESS=0,SECMODE_OPEN=1;
constexpr unsigned BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE=2;
constexpr unsigned BLE_GAP_EVT_SEC_PARAMS_REQUEST=1,BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION=19;
constexpr unsigned BLE_GATTS_EVT_HVN_TX_COMPLETE=2;
constexpr unsigned BLE_GATTS_AUTHORIZE_TYPE_READ=1,BLE_GATT_STATUS_SUCCESS=0;
constexpr unsigned BLE_GATT_STATUS_ATTERR_UNLIKELY_ERROR=14,BLE_GATT_STATUS_ATTERR_INVALID_OFFSET=7;
struct ble_gatts_evt_read_t {uint16_t offset;};
struct ble_gatts_rw_authorize_reply_params_t {
  unsigned type;
  struct {struct {
    uint16_t gatt_status,update,offset,len;
    const uint8_t* p_data;
  } read;} params;
};
static uint8_t mockReadValue[176]{};
static unsigned mockReadReplies=0,mockReadStatus=0,mockReadOffset=0;
inline err_t sd_ble_gatts_rw_authorize_reply(uint16_t,const ble_gatts_rw_authorize_reply_params_t* reply){
  ++mockReadReplies;mockReadStatus=reply->params.read.gatt_status;
  mockReadOffset=reply->params.read.offset;
  if(!mockReadStatus && reply->params.read.update &&
     reply->params.read.offset+reply->params.read.len<=sizeof(mockReadValue))
    memcpy(mockReadValue+reply->params.read.offset,reply->params.read.p_data,reply->params.read.len);
  return NRF_SUCCESS;
}
struct ble_evt_t {
  struct {unsigned evt_id;} header;
  struct {
    struct {uint16_t conn_handle;} gap_evt;
    struct {struct {struct {uint16_t count;} hvn_tx_complete;} params;} gatts_evt;
  } evt;
};
struct ble_gatts_char_handles_t {uint16_t value_handle;};
struct ble_gatts_hvx_params_t {
  uint16_t handle,type,offset;
  uint16_t* p_len;
  const uint8_t* p_data;
};
struct MockNotification {uint8_t bytes[244];unsigned length;bool accepted;};
static MockNotification mockNotifications[128]{};
static unsigned mockNotifyCalls=0,mockResets=0;
static uint16_t mockMtu=23;
static uint16_t mockDataLength=27,mockInterval=24;
static uint8_t mockPhy=1;
static bool mockLink=true,mockNotifyEnabled=true,mockNotifyAccepted=true;
static bool mockAutoComplete=true,mockResourceExhausted=false;
static bool mockConnectionAvailable=true;
static bool mockLinkRequestAccepted=true;
static unsigned mockLinkRequests[4]{};
static unsigned mockNotifyDurationUs=0;
static bool mockFeatureBeginFailure=false,mockFeatureWriteFailure=false;
static bool mockIdentityBeginFailure=false,mockIdentityWriteFailure=false;
static uint8_t mockFeatureValue[12]{},mockProtocolValue[2]{};
static uint8_t mockIdentityValue[17]{};
static unsigned mockFeatureLength=0,mockProtocolLength=0;
static unsigned mockIdentityLength=0;
static const char* mockCurrentServiceUuid=nullptr;
struct MockFicr {uint32_t DEVICEID[2]{};};
struct MockNvmc {uint32_t READY=1;};
static MockFicr mockFicr;
static MockNvmc mockNvmc;
#define NRF_FICR (&mockFicr)
#define NRF_NVMC (&mockNvmc)
inline void __DSB(){}
inline void NVIC_SystemReset(){++mockResets;}
inline err_t sd_ble_gap_disconnect(uint16_t,unsigned){mockLink=false;return NRF_SUCCESS;}
void mockTransmissionComplete(unsigned count);
inline err_t sd_ble_gatts_hvx(uint16_t,const ble_gatts_hvx_params_t* params){
  if(mockResourceExhausted)return NRF_ERROR_RESOURCES;
  delayMicroseconds(mockNotifyDurationUs);
  if(mockNotifyCalls<128){
    MockNotification& notification=mockNotifications[mockNotifyCalls];
    notification.length=*params->p_len;notification.accepted=mockNotifyAccepted;
    if(*params->p_len<=sizeof(notification.bytes))
      memcpy(notification.bytes,params->p_data,*params->p_len);
  }
  ++mockNotifyCalls;
  if(mockNotifyAccepted && mockAutoComplete)mockTransmissionComplete(1);
  return mockNotifyAccepted?NRF_SUCCESS:NRF_ERROR_INTERNAL;
}

struct BLEService {
  const char* uuid;
  explicit BLEService(const char* value):uuid(value){}
  err_t begin(){mockCurrentServiceUuid=uuid;return ERROR_NONE;}
};
struct BLECharacteristic {
  const char* uuid;
  const char* serviceUuid=nullptr;
  unsigned properties=0,readPermission=0,writePermission=0,fixedLength=0;
  explicit BLECharacteristic(const char* value):uuid(value){}
  bool isFeature() const {return !strcmp(uuid,"b973000b-7d2a-4f91-a380-8b52c029dc61");}
  bool isIdentity() const {return !strcmp(uuid,"b973000c-7d2a-4f91-a380-8b52c029dc61");}
  void setProperties(unsigned value){properties=value;}
  void setPermission(unsigned read,unsigned write){readPermission=read;writePermission=write;}
  void setMaxLen(unsigned){}
  void setFixedLen(unsigned value){fixedLength=value;}
  void setWriteCallback(void (*)(uint16_t,BLECharacteristic*,uint8_t*,uint16_t),bool){}
  void setReadAuthorizeCallback(void (*)(uint16_t,BLECharacteristic*,ble_gatts_evt_read_t*),bool){}
  err_t begin(){
    serviceUuid=mockCurrentServiceUuid;
    return (isFeature() && mockFeatureBeginFailure) || (isIdentity() && mockIdentityBeginFailure)?-1:ERROR_NONE;
  }
  unsigned write(const void* value,unsigned length){
    if(isFeature()){
      if(mockFeatureWriteFailure)return 0;
      if(length<=sizeof(mockFeatureValue))memcpy(mockFeatureValue,value,length);
      mockFeatureLength=length;
    }else if(!strcmp(uuid,"b9730006-7d2a-4f91-a380-8b52c029dc61")){
      if(length<=sizeof(mockProtocolValue))memcpy(mockProtocolValue,value,length);
      mockProtocolLength=length;
    }else if(isIdentity()){
      if(mockIdentityWriteFailure)return 0;
      if(length<=sizeof(mockIdentityValue))memcpy(mockIdentityValue,value,length);
      mockIdentityLength=length;
    }
    return length;
  }
  bool notifyEnabled(){return mockNotifyEnabled;}
  ble_gatts_char_handles_t handles(){return {1};}
  bool notify(const void* bytes,unsigned length){
    delayMicroseconds(mockNotifyDurationUs);
    if(mockNotifyCalls<128){
      MockNotification& notification=mockNotifications[mockNotifyCalls];
      notification.length=length;notification.accepted=mockNotifyAccepted;
      if(length<=sizeof(notification.bytes))memcpy(notification.bytes,bytes,length);
    }
    ++mockNotifyCalls;
    return mockNotifyAccepted;
  }
};
static char mockBleSerial[17]{},mockBleName[32]{};
struct BLEDis {
  void setManufacturer(const char*){}
  void setModel(const char*){}
  void setSerialNum(const char* value){strncpy(mockBleSerial,value,sizeof(mockBleSerial)-1);}
  void setFirmwareRev(const char*){}
  err_t begin(){return ERROR_NONE;}
};
struct BLEConnection {
  uint16_t getMtu(){return mockMtu;}
  uint16_t getDataLength(){return mockDataLength;}
  uint16_t getConnectionInterval(){return mockInterval;}
  uint8_t getPHY(){return mockPhy;}
  bool requestMtuExchange(uint16_t mtu){++mockLinkRequests[0];return mtu==247 && mockLinkRequestAccepted;}
  bool requestDataLengthUpdate(){++mockLinkRequests[1];return mockLinkRequestAccepted;}
  bool requestPHY(uint8_t phy){++mockLinkRequests[2];return phy==2 && mockLinkRequestAccepted;}
  bool requestConnectionParameter(uint16_t interval){++mockLinkRequests[3];return interval==12 && mockLinkRequestAccepted;}
};
struct MockAdvertising {
  bool running=false;
  void restartOnDisconnect(bool){}
  bool addFlags(unsigned){return true;}
  bool addService(BLEService&){return true;}
  bool addName(){return true;}
  void setIntervalMS(unsigned,unsigned){}
  bool start(unsigned){running=true;return true;}
  bool stop(){running=false;return true;}
  bool isRunning(){return running;}
};
struct MockPeripheral {
  void setDisconnectCallback(void (*)(uint16_t,uint8_t)){}
  bool setConnInterval(unsigned,unsigned){return true;}
};
struct MockBluefruit {
  MockAdvertising Advertising,ScanResponse;
  MockPeripheral Periph;
  BLEConnection connection;
  void autoConnLed(bool){}
  void configPrphConn(unsigned,unsigned,unsigned,unsigned){}
  bool begin(unsigned,unsigned){return true;}
  void setName(const char* value){strncpy(mockBleName,value,sizeof(mockBleName)-1);}
  void setEventCallback(void (*)(ble_evt_t*)){}
  bool connected(){return mockLink;}
  uint16_t connHandle(){return 0;}
  BLEConnection* Connection(uint16_t){return mockConnectionAvailable?&connection:nullptr;}
  bool disconnect(uint16_t){mockLink=false;return true;}
};
static MockBluefruit Bluefruit;
