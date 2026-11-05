#pragma once
#define I2C_MODE 1
enum { IMU_SUCCESS = 0, IMU_HW_ERROR = 1 };
extern unsigned mockImuAxisReads;
static uint32_t mockImuReadCostUs = 0;
static bool mockImuOn = true;
static int mockImuBeginStatus = 0; // Non-zero simulates an IMU initialisation failure.
static bool mockImuBadRegisterRead = false;
static uint8_t mockImuCtrl1Xl = 0, mockImuCtrl2G = 0;
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
  int begin() {
    ++mockImuBegins; mockImuOn = true;
    const uint8_t accelFs = settings.accelRange == 2 ? 0x00 :
                            settings.accelRange == 4 ? 0x08 :
                            settings.accelRange == 8 ? 0x0C : 0x04;
    const uint8_t gyroFs = settings.gyroRange == 125 ? 0x02 :
                           settings.gyroRange == 245 ? 0x00 :
                           settings.gyroRange == 500 ? 0x04 :
                           settings.gyroRange == 1000 ? 0x08 : 0x0C;
    mockImuCtrl1Xl = uint8_t((settings.accelSampleRate == 104 ? 0x40 : 0) | accelFs);
    mockImuCtrl2G = uint8_t((settings.gyroSampleRate == 104 ? 0x40 : 0) | gyroFs);
    return mockImuBeginStatus;
  }
  int readRegister(uint8_t* output, uint8_t reg) {
    if (reg != 0x10 && reg != 0x11) return IMU_HW_ERROR;
    *output = reg == 0x10 ? mockImuCtrl1Xl : mockImuCtrl2G;
    if (mockImuBadRegisterRead && reg == 0x10) *output ^= 0x04;
    return IMU_SUCCESS;
  }
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
  float calcAccel(int16_t n) { return n * settings.accelRange / 32768.0f; }
  float calcGyro(int16_t n) { return n * settings.gyroRange / 32768.0f; }
  float readFloatAccelX() { ++mockImuAxisReads; return 0.1f; }
  float readFloatAccelY() { ++mockImuAxisReads; return 0.2f; }
  float readFloatAccelZ() { ++mockImuAxisReads; return 1.0f; }
  float readFloatGyroX() { ++mockImuAxisReads; return 0.3f; }
  float readFloatGyroY() { ++mockImuAxisReads; return 0.4f; }
  float readFloatGyroZ() { ++mockImuAxisReads; return 0.5f; }
};
