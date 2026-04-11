#include "gyroscope_jerk_adapter.h"

#include <math.h>

GyroscopeJerkAdapter::GyroscopeJerkAdapter(GyroscopePort& gyroscope)
    : gyroscope_(gyroscope),
      has_previous_sample_(false),
      previous_sample_(GyroscopeSample{0.0f, 0.0f, 0.0f, 0}) {}

bool GyroscopeJerkAdapter::begin() {
  has_previous_sample_ = false;
  previous_sample_ = GyroscopeSample{0.0f, 0.0f, 0.0f, 0};
  return gyroscope_.begin();
}

void GyroscopeJerkAdapter::reset() {
  has_previous_sample_ = false;
}

bool GyroscopeJerkAdapter::readJerk(AngularJerkSample& sample) {
  GyroscopeSample current{};
  if (!gyroscope_.readAngularVelocity(current)) {
    return false;
  }

  if (!has_previous_sample_) {
    previous_sample_ = current;
    has_previous_sample_ = true;
    return false;
  }

  const uint32_t dt_ms = current.timestamp_ms - previous_sample_.timestamp_ms;
  if (dt_ms == 0) {
    return false;
  }

  const float dt_s = static_cast<float>(dt_ms) / 1000.0f;
  sample.jx = (current.gx_rad_s - previous_sample_.gx_rad_s) / dt_s;
  sample.jy = (current.gy_rad_s - previous_sample_.gy_rad_s) / dt_s;
  sample.jz = (current.gz_rad_s - previous_sample_.gz_rad_s) / dt_s;
  sample.magnitude = sqrtf(
      (sample.jx * sample.jx) +
      (sample.jy * sample.jy) +
      (sample.jz * sample.jz));
  sample.timestamp_ms = current.timestamp_ms;

  previous_sample_ = current;
  return true;
}