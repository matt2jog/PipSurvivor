#include "mpu6050_gyroscope_adapter.h"

namespace {

const uint8_t kRegPowerMgmt1 = 0x6B;
const uint8_t kRegConfig = 0x1A;
const uint8_t kRegGyroConfig = 0x1B;
const uint8_t kRegGyroXoutH = 0x43;

const float kGyroScaleLsbPerDps = 131.0f;  // +/-250 deg/s

float rawToRadPerSec(int16_t raw) {
  const float dps = static_cast<float>(raw) / kGyroScaleLsbPerDps;
  return dps * DEG_TO_RAD;
}

}  // namespace

Mpu6050GyroscopeAdapter::Mpu6050GyroscopeAdapter(TwoWire& wire, uint8_t address)
    : GyroscopePort(),
      wire_(wire),
      address_(address),
      offset_gx_rad_s_(0.0f),
      offset_gy_rad_s_(0.0f),
      offset_gz_rad_s_(0.0f) {}

bool Mpu6050GyroscopeAdapter::begin() {
  wire_.begin();

  if (!writeRegister(kRegPowerMgmt1, 0x00)) {
    return false;
  }

  // Match accel adapter low-pass profile and lowest gyro range for precision.
  if (!writeRegister(kRegConfig, 0x03)) {
    return false;
  }

  if (!writeRegister(kRegGyroConfig, 0x00)) {
    return false;
  }

  delay(50);
  return calibrate();
}

bool Mpu6050GyroscopeAdapter::calibrate(uint16_t sampleCount) {
  if (sampleCount == 0) {
    return false;
  }

  float sum_gx = 0.0f;
  float sum_gy = 0.0f;
  float sum_gz = 0.0f;

  for (uint16_t i = 0; i < sampleCount; ++i) {
    int16_t raw_gx = 0;
    int16_t raw_gy = 0;
    int16_t raw_gz = 0;
    if (!readRawGyro(raw_gx, raw_gy, raw_gz)) {
      return false;
    }

    sum_gx += rawToRadPerSec(raw_gx);
    sum_gy += rawToRadPerSec(raw_gy);
    sum_gz += rawToRadPerSec(raw_gz);
    delay(4);
  }

  offset_gx_rad_s_ = sum_gx / static_cast<float>(sampleCount);
  offset_gy_rad_s_ = sum_gy / static_cast<float>(sampleCount);
  offset_gz_rad_s_ = sum_gz / static_cast<float>(sampleCount);
  return true;
}

bool Mpu6050GyroscopeAdapter::readAngularVelocity(GyroscopeSample& sample) {
  int16_t raw_gx = 0;
  int16_t raw_gy = 0;
  int16_t raw_gz = 0;
  if (!readRawGyro(raw_gx, raw_gy, raw_gz)) {
    return false;
  }

  sample.gx_rad_s = rawToRadPerSec(raw_gx) - offset_gx_rad_s_;
  sample.gy_rad_s = rawToRadPerSec(raw_gy) - offset_gy_rad_s_;
  sample.gz_rad_s = rawToRadPerSec(raw_gz) - offset_gz_rad_s_;
  sample.timestamp_ms = millis();
  return true;
}

bool Mpu6050GyroscopeAdapter::readRawGyro(int16_t& gx, int16_t& gy, int16_t& gz) {
  uint8_t buffer[6] = {0, 0, 0, 0, 0, 0};
  if (!readRegisters(kRegGyroXoutH, buffer, sizeof(buffer))) {
    return false;
  }

  gx = static_cast<int16_t>((static_cast<uint16_t>(buffer[0]) << 8) | buffer[1]);
  gy = static_cast<int16_t>((static_cast<uint16_t>(buffer[2]) << 8) | buffer[3]);
  gz = static_cast<int16_t>((static_cast<uint16_t>(buffer[4]) << 8) | buffer[5]);
  return true;
}

bool Mpu6050GyroscopeAdapter::writeRegister(uint8_t reg, uint8_t value) {
  wire_.beginTransmission(address_);
  wire_.write(reg);
  wire_.write(value);
  return wire_.endTransmission() == 0;
}

bool Mpu6050GyroscopeAdapter::readRegisters(uint8_t startReg, uint8_t* buffer, size_t len) {
  wire_.beginTransmission(address_);
  wire_.write(startReg);
  if (wire_.endTransmission(false) != 0) {
    return false;
  }

  const uint8_t requested = static_cast<uint8_t>(len);
  const uint8_t received = wire_.requestFrom(static_cast<int>(address_), static_cast<int>(requested));
  if (received != requested) {
    return false;
  }

  for (size_t i = 0; i < len; ++i) {
    if (wire_.available() <= 0) {
      return false;
    }
    buffer[i] = static_cast<uint8_t>(wire_.read());
  }
  return true;
}