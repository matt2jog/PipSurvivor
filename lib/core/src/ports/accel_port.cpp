#include "accel_port.h"

#include <math.h>

AccelPort::AccelPort()
    : has_previous_sample_(false), previous_sample_(LinearAccelerationSample{0, 0, 0, 0}) {}

bool AccelPort::readJerk(JerkSample& sample) {
  LinearAccelerationSample current{};
  if (!readLinearAcceleration(current)) {
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
  sample.jx_mps3 = (current.ax_mps2 - previous_sample_.ax_mps2) / dt_s;
  sample.jy_mps3 = (current.ay_mps2 - previous_sample_.ay_mps2) / dt_s;
  sample.jz_mps3 = (current.az_mps2 - previous_sample_.az_mps2) / dt_s;
  sample.magnitude_mps3 = sqrtf(
      (sample.jx_mps3 * sample.jx_mps3) +
      (sample.jy_mps3 * sample.jy_mps3) +
      (sample.jz_mps3 * sample.jz_mps3));
  sample.timestamp_ms = current.timestamp_ms;

  previous_sample_ = current;
  return true;
}

void AccelPort::resetJerkBaseline() {
  has_previous_sample_ = false;
  previous_sample_ = LinearAccelerationSample{0, 0, 0, 0};
}