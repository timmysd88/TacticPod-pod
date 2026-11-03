#pragma once
#include "gps_power.h"
#if TACTICPOD_GPS_BACKUP_TEST
#include <stdint.h>
namespace GpsBackupUartDiag {
enum Tag : unsigned { BeforeStop, AfterStop, BeforeRestart, AfterRestart, FirstRxIrq, Count };
struct Snapshot {
  uint32_t us, endrx, rxdrdy, rxstarted, rxto, error, amount, errorsrc;
  uint32_t inten, enabled, irqEnabled, irqPending, irqActive, primask;
  uint32_t txAmount, endtx;
};
void start();
void capture(Tag tag);
void armFirstRx();
void stop();
bool get(Tag tag, Snapshot& value);
uint32_t errorsSeen();
void observeErrors();
}
#endif
