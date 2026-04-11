#include "alert_detection_adapter.h"

#include <math.h>

AlertDetectionMetaParams AlertDetectionAdapter::defaultMetaParams() {
  AlertDetectionMetaParams params{};
  params.enable_jerk_check = true;
  params.max_jerk_magnitude = 25.0f;
  params.enable_delta_altitude_check = true;
  params.max_abs_delta_altitude_m = 2.0f;
  params.enable_submersion_check = true;
  params.allow_submersion = false;
  params.duplicate_suppress_ms = 3000;
  params.duplicate_jerk_epsilon = 0.5f;
  params.duplicate_delta_altitude_epsilon_m = 0.2f;
  return params;
}

AlertDetectionAdapter::AlertDetectionAdapter(
    JerkPort& jerkPort,
    AltitudePort& altitudePort,
    SubmersionPort& submersionPort,
    const AlertDetectionMetaParams& params)
    : AlertDetectionPort(),
      jerk_port_(jerkPort),
      altitude_port_(altitudePort),
      submersion_port_(submersionPort),
      has_previous_altitude_(false),
      previous_altitude_m_(0.0f),
      previous_altitude_timestamp_ms_(0),
      jerk_cache_(AlertCache{false, 0.0f, false, 0}),
      altitude_cache_(AlertCache{false, 0.0f, false, 0}),
      submersion_cache_(AlertCache{false, 0.0f, false, 0}) {
  setMetaParams(params);
}

bool AlertDetectionAdapter::begin() {
  has_previous_altitude_ = false;
  previous_altitude_m_ = 0.0f;
  previous_altitude_timestamp_ms_ = 0;
  clearCache(jerk_cache_);
  clearCache(altitude_cache_);
  clearCache(submersion_cache_);

  // Continue initialization even if one source fails so diagnostics can still run.
  const bool jerk_ok = jerk_port_.begin();
  const bool altitude_ok = altitude_port_.begin();
  const bool submersion_ok = submersion_port_.begin();
  return jerk_ok && altitude_ok && submersion_ok;
}

void AlertDetectionAdapter::clearCache(AlertCache& cache) {
  cache.has_value = false;
  cache.value = 0.0f;
  cache.bool_value = false;
  cache.timestamp_ms = 0;
}

bool AlertDetectionAdapter::shouldEmitNumericAlert(
    AlertCache& cache,
    float observed,
    float epsilon,
    uint32_t nowMs) const {
  if (!cache.has_value) {
    return true;
  }

  const uint32_t age_ms = nowMs - cache.timestamp_ms;
  const bool same_window = age_ms < meta_params_.duplicate_suppress_ms;
  const bool same_value = fabsf(observed - cache.value) <= epsilon;
  return !(same_window && same_value);
}

bool AlertDetectionAdapter::shouldEmitBooleanAlert(
    AlertCache& cache,
    bool observed,
    uint32_t nowMs) const {
  if (!cache.has_value) {
    return true;
  }

  const uint32_t age_ms = nowMs - cache.timestamp_ms;
  const bool same_window = age_ms < meta_params_.duplicate_suppress_ms;
  const bool same_value = observed == cache.bool_value;
  return !(same_window && same_value);
}

bool AlertDetectionAdapter::poll(AlertDetectionReading& reading) {
  reading = AlertDetectionReading{false, 0.0f, false, 0.0f, false, false, millis()};

  AngularJerkSample jerk{};
  if (jerk_port_.readJerk(jerk)) {
    reading.has_jerk = true;
    reading.jerk_magnitude = jerk.magnitude;
    reading.timestamp_ms = jerk.timestamp_ms;
  }

  AltitudeSample altitude{};
  if (altitude_port_.readAltitude(altitude)) {
    if (has_previous_altitude_) {
      reading.has_delta_altitude = true;
      reading.delta_altitude_m = altitude.altitude_m - previous_altitude_m_;
    }
    previous_altitude_m_ = altitude.altitude_m;
    previous_altitude_timestamp_ms_ = altitude.timestamp_ms;
    has_previous_altitude_ = true;
    reading.timestamp_ms = altitude.timestamp_ms;
  }

  SubmersionSample submersion{};
  if (submersion_port_.readSubmersion(submersion)) {
    reading.has_submersion = true;
    reading.is_submerged = submersion.submerged;
    reading.timestamp_ms = submersion.timestamp_ms;
  }

  const uint32_t now_ms = reading.timestamp_ms;

  if (meta_params_.enable_jerk_check && reading.has_jerk) {
    if (reading.jerk_magnitude > meta_params_.max_jerk_magnitude) {
      if (shouldEmitNumericAlert(
              jerk_cache_,
              reading.jerk_magnitude,
              meta_params_.duplicate_jerk_epsilon,
              now_ms)) {
        const AlertDetectionEvent event = AlertDetectionEvent{
            AlertSensorMetric::JerkMagnitude,
            reading.jerk_magnitude,
            meta_params_.max_jerk_magnitude,
            true,
            reading,
        };
        emitAlert(event);
        jerk_cache_.has_value = true;
        jerk_cache_.value = reading.jerk_magnitude;
        jerk_cache_.timestamp_ms = now_ms;
      }
    } else {
      // Reset cache when metric returns to acceptable range so future violations are reported.
      clearCache(jerk_cache_);
    }
  }

  if (meta_params_.enable_delta_altitude_check && reading.has_delta_altitude) {
    const float abs_delta = fabsf(reading.delta_altitude_m);
    if (abs_delta > meta_params_.max_abs_delta_altitude_m) {
      if (shouldEmitNumericAlert(
              altitude_cache_,
              reading.delta_altitude_m,
              meta_params_.duplicate_delta_altitude_epsilon_m,
              now_ms)) {
        const AlertDetectionEvent event = AlertDetectionEvent{
            AlertSensorMetric::DeltaAltitude,
            reading.delta_altitude_m,
            meta_params_.max_abs_delta_altitude_m,
            true,
            reading,
        };
        emitAlert(event);
        altitude_cache_.has_value = true;
        altitude_cache_.value = reading.delta_altitude_m;
        altitude_cache_.timestamp_ms = now_ms;
      }
    } else {
      clearCache(altitude_cache_);
    }
  }

  if (meta_params_.enable_submersion_check && reading.has_submersion) {
    if (!meta_params_.allow_submersion && reading.is_submerged) {
      if (shouldEmitBooleanAlert(submersion_cache_, reading.is_submerged, now_ms)) {
        const AlertDetectionEvent event = AlertDetectionEvent{
            AlertSensorMetric::Submersion,
            reading.is_submerged ? 1.0f : 0.0f,
            0.0f,
            true,
            reading,
        };
        emitAlert(event);
        submersion_cache_.has_value = true;
        submersion_cache_.bool_value = reading.is_submerged;
        submersion_cache_.timestamp_ms = now_ms;
      }
    } else {
      clearCache(submersion_cache_);
    }
  }

  return reading.has_jerk || reading.has_delta_altitude || reading.has_submersion;
}