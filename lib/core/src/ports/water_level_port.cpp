#include "water_level_port.h"

WaterLevelPort::WaterLevelPort()
    : has_last_sample_(false), last_sample_(WaterLevelSample{0, 0.0f, false, 0}) {}

bool WaterLevelPort::isWet() {
  WaterLevelSample sample{};
  if (readLevel(sample)) {
    last_sample_ = sample;
    has_last_sample_ = true;
  }

  if (!has_last_sample_) {
    return false;
  }

  return last_sample_.wet;
}