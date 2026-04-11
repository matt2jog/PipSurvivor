#ifndef PIPSURVIVOR_MOCK_ACCEL_ADAPTER_H
#define PIPSURVIVOR_MOCK_ACCEL_ADAPTER_H

#include <Arduino.h>

#include "../ports/accel_port.h"

class MockAccelAdapter : public AccelPort {
 public:
  static const size_t kMaxSamples = 32;

  MockAccelAdapter();

  bool begin() override;
  bool readLinearAcceleration(LinearAccelerationSample& sample) override;

  bool queueSample(const LinearAccelerationSample& sample);

 private:
  LinearAccelerationSample queue_[kMaxSamples];
  size_t queue_count_;
};

#endif