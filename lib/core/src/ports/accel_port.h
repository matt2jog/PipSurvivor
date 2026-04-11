#ifndef PIPSURVIVOR_ACCEL_PORT_H
#define PIPSURVIVOR_ACCEL_PORT_H

#include <Arduino.h>

struct LinearAccelerationSample {
  float ax_mps2;
  float ay_mps2;
  float az_mps2;
  uint32_t timestamp_ms;
};

struct JerkSample {
  float jx_mps3;
  float jy_mps3;
  float jz_mps3;
  float magnitude_mps3;
  uint32_t timestamp_ms;
};

class AccelPort {
 public:
  AccelPort();
  virtual ~AccelPort() = default;

  virtual bool begin() = 0;
  virtual bool readLinearAcceleration(LinearAccelerationSample& sample) = 0;

  // Returns false on the first valid acceleration sample (no prior sample to diff).
  bool readJerk(JerkSample& sample);

 protected:
  void resetJerkBaseline();

 private:
  bool has_previous_sample_;
  LinearAccelerationSample previous_sample_;
};

#endif