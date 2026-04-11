#include "barometer_altitude_adapter.h"

BarometerAltitudeAdapter::BarometerAltitudeAdapter(BarometerPort& barometer)
    : barometer_(barometer),
      has_previous_altitude_(false),
      previous_sample_(AltitudeSample{0.0f, 0.0f, 0}) {}

bool BarometerAltitudeAdapter::begin() {
  has_previous_altitude_ = false;
  previous_sample_ = AltitudeSample{0.0f, 0.0f, 0};
  return barometer_.begin();
}

void BarometerAltitudeAdapter::reset() {
  has_previous_altitude_ = false;
}

bool BarometerAltitudeAdapter::readAltitude(AltitudeSample& sample) {
  BarometerSample source{};
  if (!barometer_.readSample(source)) {
    return false;
  }

  sample.altitude_m = source.altitude_m;
  sample.timestamp_ms = source.timestamp_ms;
  sample.vertical_speed_mps = 0.0f;

  if (has_previous_altitude_) {
    const uint32_t dt_ms = sample.timestamp_ms - previous_sample_.timestamp_ms;
    if (dt_ms > 0) {
      const float dt_s = static_cast<float>(dt_ms) / 1000.0f;
      sample.vertical_speed_mps = (sample.altitude_m - previous_sample_.altitude_m) / dt_s;
    }
  }

  previous_sample_ = sample;
  has_previous_altitude_ = true;
  return true;
}