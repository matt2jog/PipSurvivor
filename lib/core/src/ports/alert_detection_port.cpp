#include "alert_detection_port.h"

AlertDetectionPort::AlertDetectionPort()
    : meta_params_(AlertDetectionMetaParams{}),
      callback_(nullptr),
      callback_params_(AlertDetectionCallbackParams{"", "", 1}) {}

void AlertDetectionPort::setMetaParams(const AlertDetectionMetaParams& params) {
  meta_params_ = params;

  if (meta_params_.duplicate_jerk_epsilon < 0.0f) {
    meta_params_.duplicate_jerk_epsilon = 0.0f;
  }

  if (meta_params_.duplicate_submersion_epsilon < 0.0f) {
    meta_params_.duplicate_submersion_epsilon = 0.0f;
  }

  if (meta_params_.duplicate_fall_epsilon < 0.0f) {
    meta_params_.duplicate_fall_epsilon = 0.0f;
  }

  if (meta_params_.duplicate_orientation_epsilon < 0.0f) {
    meta_params_.duplicate_orientation_epsilon = 0.0f;
  }
}

const AlertDetectionMetaParams& AlertDetectionPort::metaParams() const {
  return meta_params_;
}

void AlertDetectionPort::setCallback(
    AlertDetectionCallback callback,
    const AlertDetectionCallbackParams& callbackParams) {
  callback_ = callback;
  callback_params_ = callbackParams;
}

void AlertDetectionPort::emitAlert(const AlertDetectionEvent& event) const {
  if (callback_ == nullptr) {
    return;
  }
  callback_(event, callback_params_);
}
