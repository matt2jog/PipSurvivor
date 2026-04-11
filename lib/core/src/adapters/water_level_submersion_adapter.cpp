#include "water_level_submersion_adapter.h"

SubmersionDetectionConfig WaterLevelSubmersionAdapter::defaultConfig() {
  SubmersionDetectionConfig cfg{};
  cfg.enter_threshold_normalized = 0.20f;
  cfg.exit_threshold_normalized = 0.12f;
  cfg.min_submersion_time_ms = 1200;
  cfg.min_dry_time_ms = 700;
  cfg.min_consecutive_samples = 3;
  cfg.evidence_mode = SubmersionEvidenceMode::Either;
  return cfg;
}

WaterLevelSubmersionAdapter::WaterLevelSubmersionAdapter(
    WaterLevelPort& source,
    const SubmersionDetectionConfig& config)
    : source_(source),
      config_(config),
      stable_submerged_(false),
      candidate_submerged_(false),
      has_candidate_(false),
      candidate_since_ms_(0),
      consecutive_samples_(0) {
  setConfig(config);
}

bool WaterLevelSubmersionAdapter::begin() {
  reset();
  return source_.begin();
}

void WaterLevelSubmersionAdapter::reset() {
  stable_submerged_ = false;
  candidate_submerged_ = false;
  has_candidate_ = false;
  candidate_since_ms_ = 0;
  consecutive_samples_ = 0;
  has_last_sample_ = false;
  last_sample_ = SubmersionSample{false, false, 0, 0.0f, 0, 0, 0};
}

void WaterLevelSubmersionAdapter::setConfig(const SubmersionDetectionConfig& config) {
  config_ = config;

  config_.enter_threshold_normalized = clamp01(config_.enter_threshold_normalized);
  config_.exit_threshold_normalized = clamp01(config_.exit_threshold_normalized);

  // Ensure hysteresis direction is always valid even if caller flips values.
  if (config_.exit_threshold_normalized > config_.enter_threshold_normalized) {
    const float tmp = config_.exit_threshold_normalized;
    config_.exit_threshold_normalized = config_.enter_threshold_normalized;
    config_.enter_threshold_normalized = tmp;
  }

  if (config_.min_consecutive_samples == 0) {
    config_.min_consecutive_samples = 1;
  }
}

const SubmersionDetectionConfig& WaterLevelSubmersionAdapter::config() const {
  return config_;
}

float WaterLevelSubmersionAdapter::clamp01(float value) {
  if (value < 0.0f) {
    return 0.0f;
  }
  if (value > 1.0f) {
    return 1.0f;
  }
  return value;
}

bool WaterLevelSubmersionAdapter::candidateFromSample(const WaterLevelSample& sample) const {
  // Hysteresis strategy:
  // - If currently dry, use the higher enter threshold.
  // - If currently submerged, use the lower exit threshold.
  // This prevents rapid toggling near the boundary.
  const float threshold = stable_submerged_
      ? config_.exit_threshold_normalized
      : config_.enter_threshold_normalized;

  const bool normalized_says_submerged = sample.normalized >= threshold;
  const bool wet_flag_says_submerged = sample.wet;

  // Evidence mode allows tuning behavior independently from sensor wiring model.
  switch (config_.evidence_mode) {
    case SubmersionEvidenceMode::UseNormalizedOnly:
      return normalized_says_submerged;
    case SubmersionEvidenceMode::UseWetFlagOnly:
      return wet_flag_says_submerged;
    case SubmersionEvidenceMode::RequireBoth:
      return normalized_says_submerged && wet_flag_says_submerged;
    case SubmersionEvidenceMode::Either:
      return normalized_says_submerged || wet_flag_says_submerged;
  }

  return normalized_says_submerged;
}

bool WaterLevelSubmersionAdapter::readSubmersion(SubmersionSample& sample) {
  WaterLevelSample level{};
  if (!source_.readLevel(level)) {
    return false;
  }

  const bool candidate = candidateFromSample(level);
  const uint32_t now_ms = level.timestamp_ms;

  // Time + sample-count debounce strategy:
  // 1) Track how long and how consistently we see a candidate state.
  // 2) Promote to stable state only after both thresholds are met.
  // This avoids false flips from droplets, ADC jitter, or intermittent contact.
  if (!has_candidate_ || candidate != candidate_submerged_) {
    has_candidate_ = true;
    candidate_submerged_ = candidate;
    candidate_since_ms_ = now_ms;
    consecutive_samples_ = 1;
  } else {
    if (consecutive_samples_ < 65535) {
      consecutive_samples_++;
    }
  }

  const uint32_t candidate_duration_ms = now_ms - candidate_since_ms_;
  const uint32_t required_time_ms = candidate_submerged_
      ? config_.min_submersion_time_ms
      : config_.min_dry_time_ms;

  if (candidate_submerged_ != stable_submerged_ &&
      consecutive_samples_ >= config_.min_consecutive_samples &&
      candidate_duration_ms >= required_time_ms) {
    stable_submerged_ = candidate_submerged_;
  }

  sample.submerged = stable_submerged_;
  sample.candidate_submerged = candidate_submerged_;
  sample.raw = level.raw;
  sample.normalized = level.normalized;
  sample.timestamp_ms = now_ms;
  sample.candidate_duration_ms = candidate_duration_ms;
  sample.consecutive_samples = consecutive_samples_;

  has_last_sample_ = true;
  last_sample_ = sample;
  return true;
}