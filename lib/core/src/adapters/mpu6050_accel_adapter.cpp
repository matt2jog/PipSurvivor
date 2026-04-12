#include "mpu6050_accel_adapter.h"

namespace {

const uint8_t kRegPowerMgmt1 = 0x6B;
const uint8_t kRegConfig = 0x1A;
const uint8_t kRegAccelConfig = 0x1C;
const uint8_t kRegIntPinCfg = 0x37;
const uint8_t kRegAccelXoutH = 0x3B;

const float kGravityMps2 = 9.80665f;
const float kAccelScaleLsbPerG = 16384.0f;  // +/-2g

float rawToMps2(int16_t raw) {
  return (static_cast<float>(raw) / kAccelScaleLsbPerG) * kGravityMps2;
}

}  // namespace

Mpu6050AccelAdapter::Mpu6050AccelAdapter(TwoWire& wire, uint8_t address)
    : wire_(wire),
      address_(address),
      offset_x_mps2_(0.0f),
      offset_y_mps2_(0.0f),
      offset_z_mps2_(0.0f) {}

bool Mpu6050AccelAdapter::begin() {
  wire_.begin();

  if (!writeRegister(kRegPowerMgmt1, 0x00)) {
    return false;
  }

  // Enable I2C Bypass for auxiliary sensors (like BMP280 on XDA/XCL)
  if (!writeRegister(kRegIntPinCfg, 0x02)) {
    return false;
  }

  // Moderate low-pass filter and +/-2g range for better jerk stability.
  if (!writeRegister(kRegConfig, 0x03)) {
    return false;
  }

  if (!writeRegister(kRegAccelConfig, 0x00)) {
    return false;
  }

  delay(50);
  return calibrate();
}

bool Mpu6050AccelAdapter::calibrate(uint16_t sampleCount) {
  if (sampleCount == 0) {
    return false;
  }

  float sum_x = 0.0f;
  float sum_y = 0.0f;
  float sum_z = 0.0f;

  for (uint16_t i = 0; i < sampleCount; ++i) {
    int16_t raw_x = 0;
    int16_t raw_y = 0;
    int16_t raw_z = 0;
    if (!readRawAccel(raw_x, raw_y, raw_z)) {
      return false;
    }

    sum_x += rawToMps2(raw_x);
    sum_y += rawToMps2(raw_y);
    sum_z += rawToMps2(raw_z);
    delay(4);
  }

  const float avg_x = sum_x / static_cast<float>(sampleCount);
  const float avg_y = sum_y / static_cast<float>(sampleCount);
  const float avg_z = sum_z / static_cast<float>(sampleCount);

  offset_x_mps2_ = avg_x;
  offset_y_mps2_ = avg_y;
  offset_z_mps2_ = (avg_z >= 0.0f) ? (avg_z - kGravityMps2) : (avg_z + kGravityMps2);
  resetJerkBaseline();
  return true;
}

bool Mpu6050AccelAdapter::readLinearAcceleration(LinearAccelerationSample& sample) {
  int16_t raw_x = 0;
  int16_t raw_y = 0;
  int16_t raw_z = 0;
  if (!readRawAccel(raw_x, raw_y, raw_z)) {
    return false;
  }

  sample.ax_mps2 = rawToMps2(raw_x) - offset_x_mps2_;
  sample.ay_mps2 = rawToMps2(raw_y) - offset_y_mps2_;
  sample.az_mps2 = rawToMps2(raw_z) - offset_z_mps2_;
  sample.timestamp_ms = millis();
  return true;
}

bool Mpu6050AccelAdapter::readRawAccel(int16_t& ax, int16_t& ay, int16_t& az) {
  uint8_t buffer[6] = {0, 0, 0, 0, 0, 0};
  if (!readRegisters(kRegAccelXoutH, buffer, sizeof(buffer))) {
    return false;
  }

  ax = static_cast<int16_t>((static_cast<uint16_t>(buffer[0]) << 8) | buffer[1]);
  ay = static_cast<int16_t>((static_cast<uint16_t>(buffer[2]) << 8) | buffer[3]);
  az = static_cast<int16_t>((static_cast<uint16_t>(buffer[4]) << 8) | buffer[5]);
  return true;
}

bool Mpu6050AccelAdapter::writeRegister(uint8_t reg, uint8_t value) {
  wire_.beginTransmission(address_);
  wire_.write(reg);
  wire_.write(value);
  return wire_.endTransmission() == 0;
}

bool Mpu6050AccelAdapter::readRegisters(uint8_t startReg, uint8_t* buffer, size_t len) {
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