#ifndef PIPSURVIVOR_ALERT_DETECTION_PORT_H
#define PIPSURVIVOR_ALERT_DETECTION_PORT_H

#include <Arduino.h>

enum class AlertSensorMetric : uint8_t {
  JerkMagnitude,
  DeltaAltitude,
  Submersion
};

struct AlertDetectionReading {
  bool has_jerk;
  float jerk_magnitude;

  bool has_delta_altitude;
  float delta_altitude_m;

  bool has_submersion;
  bool is_submerged;

  uint32_t timestamp_ms;
};

struct AlertDetectionMetaParams {
  // Sensor acceptance limits.
  bool enable_jerk_check;
  float max_jerk_magnitude;

  bool enable_delta_altitude_check;
  float max_abs_delta_altitude_m;

  bool enable_submersion_check;
  bool allow_submersion;

  // Duplicate-alert handling controls.
  uint32_t duplicate_suppress_ms;
  float duplicate_jerk_epsilon;
  float duplicate_delta_altitude_epsilon_m;
};

struct AlertDetectionCallbackParams {
  // Optional metadata passed to callback for routing/formatting.
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