#ifndef PIPSURVIVOR_ALERT_DETECTION_PORT_H
#define PIPSURVIVOR_ALERT_DETECTION_PORT_H

#include <Arduino.h>

enum class AlertSensorMetric : uint8_t {
  JerkMagnitude,
  Submersion,
  FallRapid,
  OrientationFlip
};

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

struct AlertDetectionMetaParams {
  bool enable_jerk_check;
  float max_jerk_magnitude;

  bool enable_submersion_check;
  float min_submersion_normalized;
  uint32_t min_submersion_duration_ms;

  bool enable_fall_check;
  float max_fall_speed_m_s;
  uint32_t fall_window_ms;

  bool enable_orientation_check;
  float max_orientation_change_rad_s;

  uint32_t duplicate_suppress_ms;
  float duplicate_jerk_epsilon;
  float duplicate_submersion_epsilon;
  float duplicate_fall_epsilon;
  float duplicate_orientation_epsilon;
};

struct AlertDetectionCallbackParams {
  String source;
  String channel;
  uint8_t severity;
};

struct AlertDetectionEvent {
  AlertSensorMetric metric;
  float observed_value;
  float threshold_value;
  bool threshold_is_upper_bound;
  AlertDetectionReading reading;
};

typedef void (*AlertDetectionCallback)(
    const AlertDetectionEvent& event,
    const AlertDetectionCallbackParams& callbackParams);

class AlertDetectionPort {
 public:
  AlertDetectionPort();
  virtual ~AlertDetectionPort() = default;

  virtual bool begin() = 0;
  virtual bool poll(AlertDetectionReading& reading) = 0;
  virtual const AlertDetectionReading& latestReading() const = 0;

  void setMetaParams(const AlertDetectionMetaParams& params);
  const AlertDetectionMetaParams& metaParams() const;

  void setCallback(
      AlertDetectionCallback callback,
      const AlertDetectionCallbackParams& callbackParams);

 protected:
  void emitAlert(const AlertDetectionEvent& event) const;

  AlertDetectionMetaParams meta_params_;

 private:
  AlertDetectionCallback callback_;
  AlertDetectionCallbackParams callback_params_;
};

#endif
