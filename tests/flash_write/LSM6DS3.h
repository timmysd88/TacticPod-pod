#pragma once
#define I2C_MODE 1
extern unsigned mockImuAxisReads;
static uint32_t mockImuReadCostUs = 0;
static bool mockImuOn = true;
static int mockImuBeginStatus = 0; // Non-zero simulates an IMU initialisation failure.
static unsigned mockImuPowerDowns = 0, mockImuBegins = 0, mockImuOffReads = 0;
inline int16_t simulatedAxis(unsigned axis,int16_t fallback) {
  if (!mockImuOn) ++mockImuOffReads;
#ifdef TCB_SIM_IMU_AXIS
  return TCB_SIM_IMU_AXIS(axis);
#else
  (void)axis;return fallback;
#endif
}
struct LSM6DS3 {
  struct Settings {
    int accelEnabled, gyroEnabled, accelRange, gyroRange;
    int accelSampleRate, gyroSampleRate;
  } settings;
  LSM6DS3(int, int) {}
  int begin() { ++mockImuBegins; mockImuOn = true; return mockImuBeginStatus; }
  int writeRegister(uint8_t reg, uint8_t value) {
    if ((reg == 0x10 || reg == 0x11) && value == 0) { mockImuOn = false; ++mockImuPowerDowns; }
    return 0;
  }
  int16_t readRawAccelX() { ++mockImuAxisReads; mockSubUs += mockImuReadCostUs; return simulatedAxis(0,100); }
  int16_t readRawAccelY() { ++mockImuAxisReads; mockSubUs += mockImuReadCostUs; return simulatedAxis(1,200); }
  int16_t readRawAccelZ() { ++mockImuAxisReads; mockSubUs += mockImuReadCostUs; return simulatedAxis(2,16384); }
  int16_t readRawGyroX() { ++mockImuAxisReads; mockSubUs += mockImuReadCostUs; return simulatedAxis(3,10); }
  int16_t readRawGyroY() { ++mockImuAxisReads; mockSubUs += mockImuReadCostUs; return simulatedAxis(4,20); }
  int16_t readRawGyroZ() { ++mockImuAxisReads; mockSubUs += mockImuReadCostUs; return simulatedAxis(5,30); }
  float calcAccel(int16_t n) { return n * 0.000061f; }
  float calcGyro(int16_t n) { return n * 0.00875f; }
  float readFloatAccelX() { ++mockImuAxisReads; return 0.1f; }
  float readFloatAccelY() { ++mockImuAxisReads; return 0.2f; }
  float readFloatAccelZ() { ++mockImuAxisReads; return 1.0f; }
  float readFloatGyroX() { ++mockImuAxisReads; return 0.3f; }
  float readFloatGyroY() { ++mockImuAxisReads; return 0.4f; }
  float readFloatGyroZ() { ++mockImuAxisReads; return 0.5f; }
};
