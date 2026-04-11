#ifndef PIPSURVIVOR_GYROSCOPE_PORT_H
#define PIPSURVIVOR_GYROSCOPE_PORT_H

#include <Arduino.h>

struct GyroscopeSample {
  float gx_rad_s;
  float gy_rad_s;
  float gz_rad_s;
  uint32_t timestamp_ms;
};

class GyroscopePort {
 public:
  GyroscopePort();
  virtual ~GyroscopePort() = default;

  virtual bool begin() = 0;
  virtual bool readAngularVelocity(GyroscopeSample& sample) = 0;
};

#endif