#pragma once
#include <Arduino.h>

typedef int err_t;
constexpr err_t ERROR_NONE=0,NRF_SUCCESS=0;
constexpr unsigned BLE_GATT_ATT_MTU_MAX=247,BLE_GAP_EVENT_LENGTH_MIN=6;
constexpr unsigned CHR_PROPS_WRITE=8,CHR_PROPS_READ=2,CHR_PROPS_NOTIFY=16;
constexpr unsigned SECMODE_NO_ACCESS=0,SECMODE_OPEN=1;
constexpr unsigned BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE=2;
constexpr unsigned BLE_GAP_EVT_SEC_PARAMS_REQUEST=1,BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION=19;
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
  struct {struct {uint16_t conn_handle;} gap_evt;} evt;
};
struct MockNotification {uint8_t bytes[244];unsigned length;bool accepted;};
static MockNotification mockNotifications[128]{};
static unsigned mockNotifyCalls=0,mockResets=0;
static uint16_t mockMtu=23;
static bool mockLink=true,mockNotifyEnabled=true,mockNotifyAccepted=true;
static unsigned mockNotifyDurationUs=0;
struct MockFicr {uint32_t DEVICEID[2]{};};
struct MockNvmc {uint32_t READY=1;};
static MockFicr mockFicr;
static MockNvmc mockNvmc;
#define NRF_FICR (&mockFicr)
#define NRF_NVMC (&mockNvmc)
inline void __DSB(){}
inline void NVIC_SystemReset(){++mockResets;}
inline err_t sd_ble_gap_disconnect(uint16_t,unsigned){mockLink=false;return NRF_SUCCESS;}

struct BLEService {
  explicit BLEService(const char*){}
  err_t begin(){return ERROR_NONE;}
};
struct BLECharacteristic {
  explicit BLECharacteristic(const char*){}
  void setProperties(unsigned){}
  void setPermission(unsigned,unsigned){}
  void setMaxLen(unsigned){}
  void setFixedLen(unsigned){}
  void setWriteCallback(void (*)(uint16_t,BLECharacteristic*,uint8_t*,uint16_t),bool){}
  void setReadAuthorizeCallback(void (*)(uint16_t,BLECharacteristic*,ble_gatts_evt_read_t*),bool){}
  err_t begin(){return ERROR_NONE;}
  unsigned write(const void*,unsigned length){return length;}
  bool notifyEnabled(){return mockNotifyEnabled;}
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
struct BLEDis {
  void setManufacturer(const char*){}
  void setModel(const char*){}
  void setSerialNum(const char*){}
  void setFirmwareRev(const char*){}
  err_t begin(){return ERROR_NONE;}
};
struct BLEConnection {uint16_t getMtu(){return mockMtu;}};
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
struct MockPeripheral {void setDisconnectCallback(void (*)(uint16_t,uint8_t)){}};
struct MockBluefruit {
  MockAdvertising Advertising,ScanResponse;
  MockPeripheral Periph;
  BLEConnection connection;
  void autoConnLed(bool){}
  void configPrphConn(unsigned,unsigned,unsigned,unsigned){}
  bool begin(unsigned,unsigned){return true;}
  void setName(const char*){}
  void setEventCallback(void (*)(ble_evt_t*)){}
  bool connected(){return mockLink;}
  uint16_t connHandle(){return 0;}
  BLEConnection* Connection(uint16_t){return &connection;}
  bool disconnect(uint16_t){mockLink=false;return true;}
};
static MockBluefruit Bluefruit;
