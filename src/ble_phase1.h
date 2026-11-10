#pragma once
#include "ble_mode.h"

namespace BlePhase1 {
// Adapter methods are foreground-only; callbacks report activity through the adapter.
template <class Adapter>
class Runtime {
 public:
  void update(uint32_t now, BleMode::Inputs in, Adapter& adapter) {
    if (controller.active()) {
      if (controller.current() != BleMode::State::Stopping &&
          controller.current() != BleMode::State::Reset) adapter.service(now);
      in.linkConnected = adapter.connected();
      in.validActivity = adapter.takeActivity();
      in.transferActive = adapter.transferActive();
      in.transferProgress = adapter.takeProgress();
      in.faulted = in.faulted || adapter.failed();
      in.quiescent = adapter.quiescent();
    }
    BleMode::Output out = controller.update(now, in);
    if (out.startAdvertising && !adapter.begin()) {
      in.faulted = true;
      out = controller.update(now, in);
    }
    if (out.abortTransfer) adapter.abortTransfer();
    if (out.stopAdvertising) adapter.stop();
    if (out.resetNow) adapter.reset();
  }
  bool active() const { return controller.active(); }
  BleMode::State state() const { return controller.current(); }
  StatusLed::Color ledColor(uint32_t now) const { return controller.ledColor(now); }

 private:
  BleMode::Controller controller;
};
}
