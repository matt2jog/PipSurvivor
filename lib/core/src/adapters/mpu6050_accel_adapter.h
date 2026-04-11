#ifndef PIPSURVIVOR_MPU6050_ACCEL_ADAPTER_H
#define PIPSURVIVOR_MPU6050_ACCEL_ADAPTER_H

#include <Arduino.h>
#include <Wire.h>

#include "../ports/accel_port.h"

class Mpu6050AccelAdapter : public AccelPort {
 public:
  static const uint8_t kDefaultAddress = 0x68;

  explicit Mpu6050AccelAdapter(TwoWire& wire = Wire, uint8_t address = kDefaultAddress);

  bool begin() override;
  bool readLinearAcceleration(LinearAccelerationSample& sample) override;

  bool calibrate(uint16_t sampleCount = 200);

 private:
  bool readRawAccel(int16_t& ax, int16_t& ay, int16_t& az);
  bool writeRegister(uint8_t reg, uint8_t value);
  bool readRegisters(uint8_t startReg, uint8_t* buffer, size_t len);

  TwoWire& wire_;
  uint8_t address_;

  float offset_x_mps2_;
  float offset_y_mps2_;
  float offset_z_mps2_;
};

#endif