#define TACTICPOD_REUSABLE 1
#define TACTICPOD_BATTERY_SIM 1
#define TACTICPOD_TCB3_TEST_BUILD 0
#include "../tcb3/first_console.cpp"
#include "../../src/battery_runtime.cpp"
// This fixture isolates destructive-command rejection, with no GNSS model.
// Model the new GPS readiness interface as unavailable, never bypassed.
namespace GpsReceiver {
bool prepareRecording(){return false;}
bool recordingReady(){return false;}
bool preparationFailed(){return true;}
}
