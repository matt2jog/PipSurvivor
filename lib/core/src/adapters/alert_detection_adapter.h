#ifndef PIPSURVIVOR_ALERT_DETECTION_ADAPTER_H
#define PIPSURVIVOR_ALERT_DETECTION_ADAPTER_H

#include <Arduino.h>

#include "../ports/alert_detection_port.h"
#include "../ports/altitude_port.h"
#include "../ports/jerk_port.h"
#include "../ports/submersion_port.h"

class AlertDetectionAdapter : public AlertDetectionPort {
 public:
  AlertDetectionAdapter(
      JerkPort& jerkPort,
      AltitudePort& altitudePort,
      SubmersionPort& submersionPort,
      const AlertDetectionMetaParams& params = defaultMetaParams());

  bool begin() override;
  bool poll(AlertDetectionReading& reading) override;

  static AlertDetectionMetaParams defaultMetaParams();

 private:
  struct AlertCache {
    bool has_value;
    float value;
    bool bool_value;
    uint32_t timestamp_ms;
  };

  bool shouldEmitNumericAlert(
      AlertCache& cache,
      float observed,
      float epsilon,
      uint32_t nowMs) const;

  bool shouldEmitBooleanAlert(
      AlertCache& cache,
      bool observed,
      uint32_t nowMs) const;

  void clearCache(AlertCache& cache);

  JerkPort& jerk_port_;
  AltitudePort& altitude_port_;
  SubmersionPort& submersion_port_;

  bool has_previous_altitude_;
  float previous_altitude_m_;
  uint32_t previous_altitude_timestamp_ms_;

  AlertCache jerk_cache_;
  AlertCache altitude_cache_;
  AlertCache submersion_cache_;
};

#endif