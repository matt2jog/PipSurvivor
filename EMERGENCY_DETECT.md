# Emergency Detection Plan for PipSurvivor

## Overview

Implement emergency situation detection that triggers radio alerts when human life is threatened. Alerts are grouped by type. Each alert type can be individually enabled/disabled via compile-time flags in `settings.h`.

---

## Emergency Categories

| Category | Alert Types |
|----------|-------------|
| **Physical Shock** | JerkMagnitude (high-G impact), FallRapid (rapid altitude descent), OrientationFlip (device flipped flat) |
| **Environmental Danger** | Submersion (underwater, gated by min value + min duration) |

---

## Alert Types

| Metric | Enum Value | Group | Trigger Condition |
|--------|-----------|-------|-------------------|
| High-G jerk | `JerkMagnitude` | `PHYSICAL_SHOCK` | Angular jerk magnitude exceeds `max_jerk_magnitude` |
| Rapid fall | `FallRapid` | `PHYSICAL_SHOCK` | Altitude drop rate exceeds `max_fall_speed_m_s` within `fall_window_ms` |
| Orientation flip | `OrientationFlip` | `PHYSICAL_SHOCK` | Device transitions from upright to flat (accel Z-axis ratio drops below threshold) |
| Underwater | `Submersion` | `ENV_DANGER` | `is_submerged == true` AND `normalized >= min_submersion_normalized` AND duration >= `min_submersion_duration_ms` |

---

## Implementation Phases

### Phase 1: Extend Alert Detection Infrastructure

#### 1.1 `AlertSensorMetric` enum (`alert_detection_port.h`)

Removed: `DeltaAltitude`, `ProlongedSubmersion`, `Motionless`. Altitude is still read internally for `FallRapid` but is not a standalone alert. Submersion consolidates duration gating into one alert type.

```cpp
enum class AlertSensorMetric : uint8_t {
  JerkMagnitude,
  Submersion,
  FallRapid,
  OrientationFlip
};
```

#### 1.2 `AlertDetectionMetaParams` (`alert_detection_port.h`)

Each alert type has its own `enable_*` flag and threshold fields. Removed `enable_delta_altitude_check`, `allow_submersion`, and motionless fields.

```cpp
struct AlertDetectionMetaParams {
  // Jerk (high-G impact / blackout / crash)
  bool enable_jerk_check;
  float max_jerk_magnitude;

  // Submersion (underwater, requires min value + min duration)
  bool enable_submersion_check;
  float min_submersion_normalized;
  uint32_t min_submersion_duration_ms;

  // Fall (rapid altitude descent)
  bool enable_fall_check;
  float max_fall_speed_m_s;
  uint32_t fall_window_ms;

  // Orientation (device flipped / lying flat)
  bool enable_orientation_check;
  float max_orientation_change_rad_s;

  // Duplicate-alert suppression
  uint32_t duplicate_suppress_ms;
  float duplicate_jerk_epsilon;
  float duplicate_submersion_epsilon;
  float duplicate_fall_epsilon;
  float duplicate_orientation_epsilon;
};
```

#### 1.3 `AlertDetectionReading` (`alert_detection_port.h`)

Removed: `has_delta_altitude`, `delta_altitude_m`, `has_orientation_delta`, `orientation_delta_rad`. Altitude delta is tracked internally for `FallRapid` but not exposed in the reading. Added: `submersion_normalized`, `submersion_duration_ms`, `is_flat`.

```cpp
struct AlertDetectionReading {
  bool has_jerk;
  float jerk_magnitude;

  bool has_submersion;
  bool is_submerged;
  float submersion_normalized;
  uint32_t submersion_duration_ms;

  bool has_accel;
  float accel_magnitude;
  bool is_flat;

  uint32_t timestamp_ms;
};
```

---

### Phase 2: Implement Detection Logic

#### 2.1 Constructor (`alert_detection_adapter.h`)

Keeps `JerkPort&`, `AltitudePort&`, `SubmersionPort&`. Adds `AccelPort&`. No new port types needed.

```cpp
#include "../ports/accel_port.h"

class AlertDetectionAdapter : public AlertDetectionPort {
 public:
  AlertDetectionAdapter(
      JerkPort& jerkPort,
      AltitudePort& altitudePort,
      SubmersionPort& submersionPort,
      AccelPort& accelPort,
      const AlertDetectionMetaParams& params = defaultMetaParams());

  bool begin() override;
  bool poll(AlertDetectionReading& reading) override;

  static AlertDetectionMetaParams defaultMetaParams();

 private:
  struct AlertCache { ... };

  // Existing ports
  JerkPort& jerk_port_;
  AltitudePort& altitude_port_;
  SubmersionPort& submersion_port_;

  // New port
  AccelPort& accel_port_;

  // Altitude tracking (for FallRapid, internal only)
  bool has_previous_altitude_;
  float previous_altitude_m_;
  uint32_t previous_altitude_timestamp_ms_;
  float cumulative_altitude_drop_m_;
  uint32_t fall_window_start_ms_;

  // Submersion duration tracking
  uint32_t submersion_enter_timestamp_ms_;

  // Alert caches (one per alert type)
  AlertCache jerk_cache_;
  AlertCache submersion_cache_;
  AlertCache fall_cache_;
  AlertCache orientation_cache_;
};
```

**Memory impact:** 4 `AlertCache` structs (~13 bytes each = ~52 bytes) + tracking state (~20 bytes) = ~72 bytes. Acceptable on ESP32.

#### 2.2 Detection rules

**JerkMagnitude** (existing, unchanged):
```cpp
if (meta_params_.enable_jerk_check && reading.has_jerk) {
  if (reading.jerk_magnitude > meta_params_.max_jerk_magnitude) {
    if (shouldEmitNumericAlert(jerk_cache_, reading.jerk_magnitude,
            meta_params_.duplicate_jerk_epsilon, now_ms)) {
      emitAlert({AlertSensorMetric::JerkMagnitude, reading.jerk_magnitude, ...});
    }
  } else {
    clearCache(jerk_cache_);
  }
}
```

**Submersion** (rewritten: min value + min duration gate):
```cpp
if (meta_params_.enable_submersion_check && reading.has_submersion) {
  if (reading.is_submerged &&
      reading.submersion_normalized >= meta_params_.min_submersion_normalized) {
    if (submersion_enter_timestamp_ms_ == 0) {
      submersion_enter_timestamp_ms_ = now_ms;
    }
    reading.submersion_duration_ms = now_ms - submersion_enter_timestamp_ms_;

    if (reading.submersion_duration_ms >= meta_params_.min_submersion_duration_ms) {
      if (shouldEmitNumericAlert(submersion_cache_,
              reading.submersion_normalized,
              meta_params_.duplicate_submersion_epsilon, now_ms)) {
        emitAlert({AlertSensorMetric::Submersion,
            reading.submersion_normalized, ...});
      }
    }
  } else {
    submersion_enter_timestamp_ms_ = 0;
    clearCache(submersion_cache_);
  }
}
```

**FallRapid** (new: altitude drop rate within sliding window):
```cpp
// Read altitude internally (not exposed in reading)
AltitudeSample altitude{};
if (altitude_port_.readAltitude(altitude)) {
  if (has_previous_altitude_) {
    float delta = previous_altitude_m_ - altitude.altitude_m; // positive = drop
    if (delta > 0) {
      cumulative_altitude_drop_m_ += delta;
    }
    uint32_t elapsed = altitude.timestamp_ms - fall_window_start_ms_;
    if (elapsed >= meta_params_.fall_window_ms) {
      float drop_rate = cumulative_altitude_drop_m_ / (elapsed / 1000.0f);
      if (meta_params_.enable_fall_check &&
          drop_rate > meta_params_.max_fall_speed_m_s) {
        if (shouldEmitNumericAlert(fall_cache_, drop_rate,
                meta_params_.duplicate_fall_epsilon, altitude.timestamp_ms)) {
          emitAlert({AlertSensorMetric::FallRapid, drop_rate, ...});
        }
      } else {
        clearCache(fall_cache_);
      }
      cumulative_altitude_drop_m_ = 0;
      fall_window_start_ms_ = altitude.timestamp_ms;
    }
  }
  previous_altitude_m_ = altitude.altitude_m_;
  previous_altitude_timestamp_ms_ = altitude.timestamp_ms;
  has_previous_altitude_ = true;
}
```

**OrientationFlip** (new: detect device lying flat from accel):
```cpp
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
    reading.is_flat = (z_ratio < 0.3f);  // Z-axis no longer dominant = flipped
  }

  if (meta_params_.enable_orientation_check && reading.is_flat) {
    if (shouldEmitNumericAlert(orientation_cache_, 1.0f,
            meta_params_.duplicate_orientation_epsilon, accel_sample.timestamp_ms)) {
      emitAlert({AlertSensorMetric::OrientationFlip, 1.0f, ...});
    }
  } else {
    clearCache(orientation_cache_);
  }

  reading.timestamp_ms = accel_sample.timestamp_ms;
}
```

---

### Phase 3: Integrate with MainDevice

#### 3.1 Update `metricName()` in `main.cpp` (line 28)

```cpp
const char* metricName(AlertSensorMetric metric) {
  switch (metric) {
    case AlertSensorMetric::JerkMagnitude:
      return "jerk";
    case AlertSensorMetric::Submersion:
      return "submerged";
    case AlertSensorMetric::FallRapid:
      return "fall";
    case AlertSensorMetric::OrientationFlip:
      return "flip";
  }
  return "metric";
}
```

#### 3.2 Update `onAlert()` formatting in `main.cpp` (line 220)

```cpp
void onAlert(
    const AlertDetectionEvent& event,
    const AlertDetectionCallbackParams& callbackParams) {

  String alert_group;
  switch (event.metric) {
    case AlertSensorMetric::JerkMagnitude:
    case AlertSensorMetric::FallRapid:
    case AlertSensorMetric::OrientationFlip:
      alert_group = "PHYSICAL_SHOCK";
      break;
    case AlertSensorMetric::Submersion:
      alert_group = "ENV_DANGER";
      break;
    default:
      alert_group = "ALERT";
  }

  String payload = alert_group + ":" + metricName(event.metric)
      + "=" + String(event.observed_value, 2);
  enqueueAlert(payload);
}
```

#### 3.3 Add `Mpu6050AccelAdapter` global and update `g_alert_detection` construction

**New include and global** (in `main.cpp`):
```cpp
#include "adapters/mpu6050_accel_adapter.h"

Mpu6050AccelAdapter g_accel;
```

**Updated construction** (line 742):
```cpp
AlertDetectionAdapter g_alert_detection(
    g_jerk,
    g_altitude,
    g_submersion,
    g_accel,
    DeviceSettings::buildAlertMetaParams());
```

#### 3.4 Update `MainDevice::begin()` to init `g_accel`

Add `g_accel.begin()` alongside existing sensor init, gated by `kEnableGyroscope` (accel shares the MPU6050 with gyro).

#### 3.5 Individual alert enable flags in `settings.h`

See settings.h changes below. Each alert type has a `kEnable*Alert` compile-time flag that feeds into `buildAlertMetaParams()`.

---

## File Changes Summary

| File | Changes |
|------|---------|
| `lib/core/src/ports/alert_detection_port.h` | Rewrite `AlertSensorMetric` (4 values); rewrite `AlertDetectionMetaParams` (per-alert enable + thresholds + duplicate epsilons); rewrite `AlertDetectionReading` (simplified) |
| `lib/core/src/adapters/alert_detection_adapter.h` | Add `AccelPort&`; add `submersion_enter_timestamp_ms_`, `cumulative_altitude_drop_m_`, `fall_window_start_ms_`; repurpose caches for 4 alert types |
| `lib/core/src/adapters/alert_detection_adapter.cpp` | Rewrite `poll()` with new detection logic; update `defaultMetaParams()`; update `begin()` |
| `lib/core/src/settings.h` | Add `kEnableJerkAlert`, `kEnableSubmersionAlert`, `kEnableFallAlert`, `kEnableOrientationAlert` flags; rewrite `buildAlertMetaParams()` |
| `src/main.cpp` | Add `#include "mpu6050_accel_adapter.h"`; add `Mpu6050AccelAdapter g_accel` global; update `g_alert_detection` construction; update `metricName()` switch; update `onAlert()` formatting |

### No new files needed

All detection logic fits into the existing adapter/port pattern. No new ports or adapter classes required.

---

## Notes

- **DeltaAltitude removed**: Altitude delta is still tracked internally by `AlertDetectionAdapter` for `FallRapid` detection but is no longer a standalone alert metric. This removes the "rapid altitude change" alert that was noisy and redundant with `FallRapid`.

- **Submersion consolidation**: The previous `Submersion` (immediate) and `ProlongedSubmersion` (duration) are merged into a single `Submersion` alert that requires both a minimum normalized water level (`min_submersion_normalized`) AND a minimum duration (`min_submersion_duration_ms`). This prevents false alerts from brief splashes.

- **Motionless removed**: Motionless detection was removed to keep the initial scope focused on life-threatening emergencies. Can be re-added later if needed.

- **AccelPort shares MPU6050** with `Mpu6050GyroscopeAdapter`. Both coexist on I2C at address 0x68. No conflict — accel reads `ACCEL_XOUT_H`, gyro reads `GYRO_XOUT_H`.

- **`begin()` ordering**: `g_accel.begin()` should be called after `g_gyro.begin()` since both configure the same chip.

- **Compounded emergencies**: Can be detected by correlating timestamps in the callback:
  - Fall + jerk spike within 500ms → fall with impact
  - Submersion + orientation flip → fell into water
