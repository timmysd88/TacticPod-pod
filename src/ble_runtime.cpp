#if TACTICPOD_BLE
#include <Arduino.h>
#include <bluefruit.h>
#include <InternalFileSystem.h>
#include <flash/flash_nrf5x.h>
#include <atomic>
#include "battery_runtime.h"
#include "ble_phase1.h"
#include "ble_runtime.h"

namespace {
constexpr uint32_t kResetMarker = 0x54424C45;
struct ResetMarker { uint32_t magic, inverse; };
volatile ResetMarker resetMarker __attribute__((section(".ble_reset"), used));
constexpr const char* kServiceUuid = "b9730001-7d2a-4f91-a380-8b52c029dc61";
constexpr const char* kActivityUuid = "b9730002-7d2a-4f91-a380-8b52c029dc61";
constexpr const char* kBatteryUuid = "b9730003-7d2a-4f91-a380-8b52c029dc61";
BLEService service(kServiceUuid);
BLECharacteristic activity(kActivityUuid);
BLECharacteristic battery(kBatteryUuid);
BLEDis deviceInfo;
std::atomic<uint32_t> events{0};
constexpr uint32_t kActivity = 1, kDisconnected = 2, kInvalid = 4, kPairing = 8;
bool started = false, failed = false;
char podId[17];

void onDisconnect(uint16_t, uint8_t) { events.fetch_or(kDisconnected); }
void onWrite(uint16_t, BLECharacteristic*, uint8_t* data, uint16_t len) {
  events.fetch_or(len == 1 && data[0] == 1 ? kActivity : kInvalid);
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
  bool begin() {
    Bluefruit.autoConnLed(false);
    // One peripheral link, no central links, no high-throughput configuration in Phase 1.
    started = true; // A partially initialized stack still needs a reset on failure.
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
    deviceInfo.setFirmwareRev("ble-phase1-1");
    if (deviceInfo.begin() != ERROR_NONE || service.begin() != ERROR_NONE)
      return fail("GATT service initialization");
    activity.setProperties(CHR_PROPS_WRITE);
    activity.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
    activity.setMaxLen(20);
    activity.setWriteCallback(onWrite, false);
    battery.setProperties(CHR_PROPS_READ);
    battery.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    battery.setFixedLen(6);
    if (activity.begin() != ERROR_NONE || battery.begin() != ERROR_NONE)
      return fail("GATT characteristic initialization");
    if (!updateBattery()) return false;
    if (!Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE) ||
        !Bluefruit.Advertising.addService(service) || !Bluefruit.ScanResponse.addName())
      return fail("advertising payload");
    Bluefruit.Advertising.setIntervalMS(100, 100);
    if (!Bluefruit.Advertising.start(180)) return fail("advertising start");
    Serial.println("BLE advertising; no session transfer in Phase 1.");
    return true;
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
    if (pending & kInvalid) Serial.println("BLE refused invalid activity; expected byte 0x01.");
    if (pending & kPairing) {
      Serial.println("BLE refused pairing: Phase 1 supports unbonded connections only.");
      ::failed = true;
    }
    return (pending & kActivity) != 0;
  }
  bool failed() { return ::failed; }
  void stop() {
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
