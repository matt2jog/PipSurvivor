#ifndef PIPSURVIVOR_MPU6050_GYROSCOPE_ADAPTER_H
#define PIPSURVIVOR_MPU6050_GYROSCOPE_ADAPTER_H

#include <Arduino.h>
#include <Wire.h>

#include "../ports/gyroscope_port.h"

class Mpu6050GyroscopeAdapter : public GyroscopePort {
 public:
  static const uint8_t kDefaultAddress = 0x68;

  explicit Mpu6050GyroscopeAdapter(TwoWire& wire = Wire, uint8_t address = kDefaultAddress);

  bool begin() override;
  bool readAngularVelocity(GyroscopeSample& sample) override;

  bool calibrate(uint16_t sampleCount = 200);

 private:
  bool readRawGyro(int16_t& gx, int16_t& gy, int16_t& gz);
  bool writeRegister(uint8_t reg, uint8_t value);
  bool readRegisters(uint8_t startReg, uint8_t* buffer, size_t len);

  TwoWire& wire_;
  uint8_t address_;

  float offset_gx_rad_s_;
  float offset_gy_rad_s_;
  float offset_gz_rad_s_;
};

#endif