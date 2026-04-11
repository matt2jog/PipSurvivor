#include "mock_accel_adapter.h"

MockAccelAdapter::MockAccelAdapter() : queue_count_(0) {}

bool MockAccelAdapter::begin() {
  resetJerkBaseline();
  queue_count_ = 0;
  return true;
}

bool MockAccelAdapter::queueSample(const LinearAccelerationSample& sample) {
  if (queue_count_ >= kMaxSamples) {
    return false;
  }

  queue_[queue_count_] = sample;
  queue_count_++;
  return true;
}

bool MockAccelAdapter::readLinearAcceleration(LinearAccelerationSample& sample) {
  if (queue_count_ == 0) {
    return false;
  }

  sample = queue_[0];
  for (size_t i = 1; i < queue_count_; ++i) {
    queue_[i - 1] = queue_[i];
  }
  queue_count_--;
  return true;
}