#include "tzt_water_level_adapter.h"

namespace {

template <typename T>
T clampValue(T v, T lo, T hi) {
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}

}  // namespace

TztWaterLevelAdapter::TztWaterLevelAdapter(
    uint8_t analogPin,
    int digitalPin,
    int powerPin,
    uint16_t dryRaw,
    uint16_t wetRaw,
    bool digitalWetHigh,
    uint16_t wetThresholdRaw,
    uint16_t settleDelayMs)
    : analog_pin_(analogPin),
      digital_pin_(digitalPin),
      power_pin_(powerPin),
      dry_raw_(dryRaw),
      wet_raw_(wetRaw),
      digital_wet_high_(digitalWetHigh),
      wet_threshold_raw_(wetThresholdRaw),
      settle_delay_ms_(settleDelayMs) {}

bool TztWaterLevelAdapter::begin() {
  pinMode(analog_pin_, INPUT);
  if (digital_pin_ >= 0) {
    pinMode(static_cast<uint8_t>(digital_pin_), INPUT);
  }
  if (power_pin_ >= 0) {
    pinMode(static_cast<uint8_t>(power_pin_), OUTPUT);
    digitalWrite(static_cast<uint8_t>(power_pin_), LOW);
  }
  return true;
}

void TztWaterLevelAdapter::setCalibration(uint16_t dryRaw, uint16_t wetRaw) {
  dry_raw_ = dryRaw;
  wet_raw_ = wetRaw;
}

bool TztWaterLevelAdapter::readLevel(WaterLevelSample& sample) {
  if (power_pin_ >= 0) {
    digitalWrite(static_cast<uint8_t>(power_pin_), HIGH);
    delay(settle_delay_ms_);
  }

  const uint16_t raw = static_cast<uint16_t>(analogRead(analog_pin_));
  const float level = normalize(raw);

  bool wet = raw >= wet_threshold_raw_;
  if (digital_pin_ >= 0) {
    const bool digital_state = digitalRead(static_cast<uint8_t>(digital_pin_)) == HIGH;
    wet = digital_wet_high_ ? digital_state : !digital_state;
  }

  sample.raw = raw;
  sample.normalized = level;
  sample.wet = wet;
  sample.timestamp_ms = millis();

  if (power_pin_ >= 0) {
    digitalWrite(static_cast<uint8_t>(power_pin_), LOW);
  }

  has_last_sample_ = true;
  last_sample_ = sample;
  return true;
}

float TztWaterLevelAdapter::normalize(uint16_t raw) const {
  if (wet_raw_ == dry_raw_) {
    return 0.0f;
  }

  const int32_t span = static_cast<int32_t>(wet_raw_) - static_cast<int32_t>(dry_raw_);
  const int32_t offset = static_cast<int32_t>(raw) - static_cast<int32_t>(dry_raw_);
  const float value = static_cast<float>(offset) / static_cast<float>(span);
  return clampValue<float>(value, 0.0f, 1.0f);
}