#pragma once
#include <stdint.h>
#include <Arduino.h>
#include <string.h>
// OFF in the default environment. Enabled only by explicit test configurations.
#ifndef TACTICPOD_TCB3_FLASH_ENABLED
#define TACTICPOD_TCB3_FLASH_ENABLED 0
#endif
#if TACTICPOD_TCB3_FLASH_ENABLED
#include "tcb3_store.h"
#endif
namespace Tcb3Session {
#if TACTICPOD_TCB3_FLASH_ENABLED
void begin(bool imuReady);
void update(bool allowWork=true);
bool exclusive();
bool exporting();
bool busy();
bool recording();
bool faulted();
bool takeCleanCompletion();
bool handleCommand(const char*);
bool startFromButton();
uint32_t freeSectors();
bool guardCommand(const char*);
void observation(unsigned kind);
void noteMissed(uint32_t);
#if TACTICPOD_BLE
bool bleBeginList();
bool bleService(unsigned steps);
bool bleListDone();
bool bleListFailed();
bool bleListOverflow();
uint32_t bleIncompleteCount();
unsigned bleSessionCount();
const Tcb3::ManifestEntry* bleSession(unsigned index);
bool bleBeginSession(uint32_t sessionId,uint32_t resetEpoch);
const uint8_t* blePacket(unsigned& length);
void bleReleasePacket();
bool bleTransferDone();
bool bleTransferFailed();
void bleFinishList();
void bleFinishSession();
void bleCancel();
#endif
#else
inline void begin(bool){}
inline void update(bool=true){}
inline bool exclusive(){return false;}
inline bool exporting(){return false;}
inline bool busy(){return false;}
inline bool recording(){return false;}
inline bool faulted(){return false;}
inline bool takeCleanCompletion(){return false;}
inline bool startFromButton(){return false;}
inline uint32_t freeSectors(){return 0;}
inline bool guardCommand(const char*){return false;}
inline void observation(unsigned){}
inline void noteMissed(uint32_t){}
inline bool handleCommand(const char* p){
  if(strncmp(p,"TCB ",4))return false;
  Serial.println("TCB DISABLED: simulated validation only; physical recording/export requires a separately approved build.");return true;
}
#endif
}
