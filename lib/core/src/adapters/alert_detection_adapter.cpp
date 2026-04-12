#include "alert_detection_adapter.h"

#include <math.h>

AlertDetectionMetaParams AlertDetectionAdapter::defaultMetaParams() {
  AlertDetectionMetaParams params{};
  params.enable_jerk_check = true;
  params.max_jerk_magnitude = 25.0f;

  params.enable_submersion_check = true;
  params.min_submersion_normalized = 0.25f;
  params.min_submersion_duration_ms = 4000;

  params.enable_fall_check = true;
  params.max_fall_speed_m_s = 2.0f;
  params.fall_window_ms = 3000;

  params.enable_orientation_check = true;
  params.max_orientation_change_rad_s = 3.0f;

  params.duplicate_suppress_ms = 4000;
  params.duplicate_jerk_epsilon = 0.4f;
  params.duplicate_submersion_epsilon = 0.1f;
  params.duplicate_fall_epsilon = 0.5f;
  params.duplicate_orientation_epsilon = 0.5f;
  return params;
}

AlertDetectionAdapter::AlertDetectionAdapter(
    JerkPort& jerkPort,
    AltitudePort& altitudePort,
    SubmersionPort& submersionPort,
    AccelPort& accelPort,
    const AlertDetectionMetaParams& params)
    : AlertDetectionPort(),
      jerk_port_(jerkPort),
      altitude_port_(altitudePort),
      submersion_port_(submersionPort),
      accel_port_(accelPort),
      has_previous_altitude_(false),
      previous_altitude_m_(0.0f),
      previous_altitude_timestamp_ms_(0),
      cumulative_altitude_drop_m_(0.0f),
      fall_window_start_ms_(0),
      submersion_enter_timestamp_ms_(0),
      jerk_cache_({false, 0.0f, 0}),
      submersion_cache_({false, 0.0f, 0}),
      fall_cache_({false, 0.0f, 0}),
      orientation_cache_({false, 0.0f, 0}) {
  setMetaParams(params);
}

bool AlertDetectionAdapter::begin() {
  has_previous_altitude_ = false;
  previous_altitude_m_ = 0.0f;
  previous_altitude_timestamp_ms_ = 0;
  cumulative_altitude_drop_m_ = 0.0f;
  fall_window_start_ms_ = 0;
  submersion_enter_timestamp_ms_ = 0;
  clearCache(jerk_cache_);
  clearCache(submersion_cache_);
  clearCache(fall_cache_);
  clearCache(orientation_cache_);
  return true;
}

void AlertDetectionAdapter::clearCache(AlertCache& cache) {
  cache.has_value = false;
  cache.value = 0.0f;
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

bool AlertDetectionAdapter::poll(AlertDetectionReading& reading) {
  reading = AlertDetectionReading{};
  reading.timestamp_ms = millis();

  AngularJerkSample jerk{};
  if (jerk_port_.readJerk(jerk)) {
    reading.has_jerk = true;
    reading.jerk_magnitude = jerk.magnitude;
    reading.timestamp_ms = jerk.timestamp_ms;
  }

  SubmersionSample submersion{};
  if (submersion_port_.readSubmersion(submersion)) {
    reading.has_submersion = true;
    reading.is_submerged = submersion.submerged;
    reading.submersion_normalized = submersion.normalized;
    reading.submersion_duration_ms = 0;
    reading.timestamp_ms = submersion.timestamp_ms;
  }

  AltitudeSample altitude{};
  if (altitude_port_.readAltitude(altitude)) {
    if (has_previous_altitude_) {
      float delta = previous_altitude_m_ - altitude.altitude_m;
      if (delta > 0.0f) {
        cumulative_altitude_drop_m_ += delta;
      }

      uint32_t elapsed = altitude.timestamp_ms - fall_window_start_ms_;
      if (elapsed >= meta_params_.fall_window_ms && elapsed > 0) {
        float drop_rate = cumulative_altitude_drop_m_ / (elapsed / 1000.0f);
        if (meta_params_.enable_fall_check &&
            drop_rate > meta_params_.max_fall_speed_m_s) {
          if (shouldEmitNumericAlert(fall_cache_, drop_rate,
                  meta_params_.duplicate_fall_epsilon, altitude.timestamp_ms)) {
            emitAlert({AlertSensorMetric::FallRapid,
                drop_rate, meta_params_.max_fall_speed_m_s, true, reading});
            fall_cache_.has_value = true;
            fall_cache_.value = drop_rate;
            fall_cache_.timestamp_ms = altitude.timestamp_ms;
          }
        } else {
          clearCache(fall_cache_);
        }
        cumulative_altitude_drop_m_ = 0.0f;
        fall_window_start_ms_ = altitude.timestamp_ms;
      }
    } else {
      fall_window_start_ms_ = altitude.timestamp_ms;
    }
    previous_altitude_m_ = altitude.altitude_m;
    previous_altitude_timestamp_ms_ = altitude.timestamp_ms;
    has_previous_altitude_ = true;
    reading.timestamp_ms = altitude.timestamp_ms;
  }

  LinearAccelerationSample accel_sample{};
  if (accel_port_.readLinearAcceleration(accel_sample)) {
    reading.has_accel = true;
    reading.accel_magnitude = sqrtf(
        accel_sample.ax_mps2 * accel_sample.ax_mps2 +
        accel_sample.ay_mps2 * accel_sample.ay_mps2 +
        accel_sample.az_mps2 * accel_sample.az_mps2);

    float total = reading.accel_magnitude;
    if (total > 0.1f) {
      float z_ratio = fabsf(accel_sample.az_mps2) / total;
      reading.is_flat = (z_ratio < 0.3f);
    }

    if (meta_params_.enable_orientation_check && reading.is_flat) {
      if (shouldEmitNumericAlert(orientation_cache_, 1.0f,
              meta_params_.duplicate_orientation_epsilon, accel_sample.timestamp_ms)) {
        emitAlert({AlertSensorMetric::OrientationFlip,
            1.0f, 0.3f, true, reading});
        orientation_cache_.has_value = true;
        orientation_cache_.value = 1.0f;
        orientation_cache_.timestamp_ms = accel_sample.timestamp_ms;
      }
    } else if (!reading.is_flat) {
      clearCache(orientation_cache_);
    }

    reading.timestamp_ms = accel_sample.timestamp_ms;
  }

  const uint32_t now_ms = reading.timestamp_ms;

  if (meta_params_.enable_jerk_check && reading.has_jerk) {
    if (reading.jerk_magnitude > meta_params_.max_jerk_magnitude) {
      if (shouldEmitNumericAlert(jerk_cache_, reading.jerk_magnitude,
              meta_params_.duplicate_jerk_epsilon, now_ms)) {
        emitAlert({AlertSensorMetric::JerkMagnitude,
            reading.jerk_magnitude, meta_params_.max_jerk_magnitude, true, reading});
        jerk_cache_.has_value = true;
        jerk_cache_.value = reading.jerk_magnitude;
        jerk_cache_.timestamp_ms = now_ms;
      }
    } else {
      clearCache(jerk_cache_);
    }
  }

  if (meta_params_.enable_submersion_check && reading.has_submersion) {
    if (reading.is_submerged &&
        reading.submersion_normalized >= meta_params_.min_submersion_normalized) {
      if (submersion_enter_timestamp_ms_ == 0) {
        submersion_enter_timestamp_ms_ = now_ms;
      }
      reading.submersion_duration_ms = now_ms - submersion_enter_timestamp_ms_;

      if (reading.submersion_duration_ms >= meta_params_.min_submersion_duration_ms) {
        if (shouldEmitNumericAlert(submersion_cache_, reading.submersion_normalized,
                meta_params_.duplicate_submersion_epsilon, now_ms)) {
          emitAlert({AlertSensorMetric::Submersion,
              reading.submersion_normalized,
              meta_params_.min_submersion_normalized, true, reading});
          submersion_cache_.has_value = true;
          submersion_cache_.value = reading.submersion_normalized;
          submersion_cache_.timestamp_ms = now_ms;
        }
      }
    } else {
      submersion_enter_timestamp_ms_ = 0;
      clearCache(submersion_cache_);
    }
  }

  latest_reading_ = reading;
  return reading.has_jerk || reading.has_submersion || reading.has_accel;
}

const AlertDetectionReading& AlertDetectionAdapter::latestReading() const {
  return latest_reading_;
}
