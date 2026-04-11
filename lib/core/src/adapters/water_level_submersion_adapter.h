#ifndef PIPSURVIVOR_WATER_LEVEL_SUBMERSION_ADAPTER_H
#define PIPSURVIVOR_WATER_LEVEL_SUBMERSION_ADAPTER_H

#include <Arduino.h>

#include "../ports/submersion_port.h"
#include "../ports/water_level_port.h"

enum class SubmersionEvidenceMode : uint8_t {
  UseNormalizedOnly,
  UseWetFlagOnly,
  RequireBoth,
  Either
};

struct SubmersionDetectionConfig {
  // Hysteresis thresholds on normalized range [0.0, 1.0].
  // Enter threshold should be >= exit threshold to reduce state chatter.
  float enter_threshold_normalized;
  float exit_threshold_normalized;

  // Debounce windows for stable state transitions.
  uint32_t min_submersion_time_ms;
  uint32_t min_dry_time_ms;

  // Additional sample-count gate to reject one-off spikes.
  uint16_t min_consecutive_samples;

  // Controls how analog and boolean wet evidence are combined.
  SubmersionEvidenceMode evidence_mode;
};

class WaterLevelSubmersionAdapter : public SubmersionPort {
 public:
  explicit WaterLevelSubmersionAdapter(
      WaterLevelPort& source,
      const SubmersionDetectionConfig& config = defaultConfig());

  bool begin() override;
  bool readSubmersion(SubmersionSample& sample) override;

  void setConfig(const SubmersionDetectionConfig& config);
  const SubmersionDetectionConfig& config() const;

  void reset();

  static SubmersionDetectionConfig defaultConfig();

 private:
  bool candidateFromSample(const WaterLevelSample& sample) const;
  static float clamp01(float value);

  WaterLevelPort& source_;
  SubmersionDetectionConfig config_;

  bool stable_submerged_;
  bool candidate_submerged_;
  bool has_candidate_;
  uint32_t candidate_since_ms_;
  uint16_t consecutive_samples_;
};

#endif