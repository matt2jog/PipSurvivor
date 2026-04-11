#include <Arduino.h>
#include <Wire.h>

#include "adapters/alert_detection_adapter.h"
#include "adapters/barometer_altitude_adapter.h"
#include "adapters/bmp280_barometer_adapter.h"
#include "adapters/gyroscope_jerk_adapter.h"
#include "adapters/matrix_button_panel_adapter.h"
#include "adapters/mock_display_adapter.h"
#include "adapters/mock_radio_adapter.h"
#include "adapters/espnow_radio_adapter.h"
#include "adapters/mpu6050_gyroscope_adapter.h"
#include "adapters/rylr998_radio_adapter.h"
#include "adapters/tzt_water_level_adapter.h"
#include "adapters/water_level_submersion_adapter.h"
#include "ports/alert_detection_port.h"
#include "ports/button_panel_port.h"
#include "ports/display_port.h"
#include "ports/radio_port.h"
#include "settings.h"

#include <string.h>

namespace {

const char* metricName(AlertSensorMetric metric) {
  switch (metric) {
    case AlertSensorMetric::JerkMagnitude:
      return "jerk";
    case AlertSensorMetric::DeltaAltitude:
      return "dAlt";
    case AlertSensorMetric::Submersion:
      return "submerged";
  }
  return "metric";
}

bool isNumericKey(KeyInput key) {
  return key >= KeyInput::K0 && key <= KeyInput::K9;
}

const char* multiTapChars(KeyInput key) {
  switch (key) {
    case KeyInput::K0:
      return " 0";
    case KeyInput::K1:
      return "1.,!?";
    case KeyInput::K2:
      return "ABC2";
    case KeyInput::K3:
      return "DEF3";
    case KeyInput::K4:
      return "GHI4";
    case KeyInput::K5:
      return "JKL5";
    case KeyInput::K6:
      return "MNO6";
    case KeyInput::K7:
      return "PQRS7";
    case KeyInput::K8:
      return "TUV8";
    case KeyInput::K9:
      return "WXYZ9";
    default:
      return "";
  }
}

class MainDevice {
 public:
  MainDevice(
      DisplayPort& display,
      ButtonPanelPort& buttons,
      RadioPort& radio,
      AlertDetectionPort& alertDetection)
      : display_(display),
        buttons_(buttons),
        radio_(radio),
        alert_detection_(alertDetection),
        state_(DisplayState::Initial),
        previous_state_(DisplayState::Initial),
        message_count_(0),
        selected_message_index_(0),
        selected_text_offset_(0),
        alert_queue_count_(0),
        last_display_render_ms_(0),
        last_alert_poll_ms_(0),
        pending_key_(KeyInput::None),
        pending_char_index_(0),
        pending_char_pos_(-1),
        pending_updated_ms_(0),
        render_dirty_(true),
        last_rendered_frame_(DisplayFrame{"", ""}) {}

  static void setInstance(MainDevice* instance) {
    instance_ = instance;
  }

  static MainDevice* instance() {
    return instance_;
  }

  bool begin() {
    display_.clear();

    const bool display_ok = true;
    const bool buttons_ok = buttons_.begin();
    const bool radio_ok = true;
    const bool alert_ok = alert_detection_.begin();

    radio_.setReceiveCallback(MainDevice::onRadioMessageStatic);

    AlertDetectionCallbackParams cb{};
    cb.source = "main_device";
    cb.channel = "radio-alert";
    cb.severity = 2;
    alert_detection_.setCallback(MainDevice::onAlertStatic, cb);

    addFeedMessage("System ready");
    render_dirty_ = true;

    return display_ok && buttons_ok && radio_ok && alert_ok;
  }

  void loop() {
    radio_.poll();

    const uint32_t now = millis();
    if ((now - last_alert_poll_ms_) >= DeviceSettings::kAlertPollMs) {
      AlertDetectionReading reading{};
      alert_detection_.poll(reading);
      last_alert_poll_ms_ = now;
    }

    processQueuedAlerts();
    maybeCommitPendingMultiTap(now);

    KeyInput key = KeyInput::None;
    if (buttons_.readKey(key)) {
      handleKey(key);
      render_dirty_ = true;
    }

    if (render_dirty_ || (now - last_display_render_ms_) >= DeviceSettings::kDisplayRefreshMs) {
      render();
      last_display_render_ms_ = now;
      render_dirty_ = false;
    }
  }

 private:
  static MainDevice* instance_;

  static void onRadioMessageStatic(const String& message) {
    if (instance_ != nullptr) {
      instance_->onRadioMessage(message);
    }
  }

  static void onAlertStatic(
      const AlertDetectionEvent& event,
      const AlertDetectionCallbackParams& callbackParams) {
    if (instance_ != nullptr) {
      instance_->onAlert(event, callbackParams);
    }
  }

  void onRadioMessage(const String& message) {
    addFeedMessage("RX:" + message);
    render_dirty_ = true;
  }

  void onAlert(
      const AlertDetectionEvent& event,
      const AlertDetectionCallbackParams& callbackParams) {
    String payload = "ALR[";
    payload += callbackParams.source;
    payload += "] ";
    payload += metricName(event.metric);
    payload += "=";
    payload += String(event.observed_value, 2);
    enqueueAlert(payload);
  }

  void enqueueAlert(const String& alert) {
    if (alert_queue_count_ < DeviceSettings::kMaxQueuedAlerts) {
      alert_queue_[alert_queue_count_] = alert;
      alert_queue_count_++;
      return;
    }

    for (size_t i = 1; i < DeviceSettings::kMaxQueuedAlerts; ++i) {
      alert_queue_[i - 1] = alert_queue_[i];
    }
    alert_queue_[DeviceSettings::kMaxQueuedAlerts - 1] = alert;
  }

  void processQueuedAlerts() {
    if (alert_queue_count_ == 0) {
      return;
    }

    const String alert = alert_queue_[0];
    for (size_t i = 1; i < alert_queue_count_; ++i) {
      alert_queue_[i - 1] = alert_queue_[i];
    }
    alert_queue_count_--;

    addFeedMessage(alert);
    if (!radio_.sendAlert(alert, 1)) {
      enterError("alert send fail");
    }
  }

  void enterError(const String& reason) {
    previous_state_ = state_;
    state_ = DisplayState::Error;
    error_message_ = reason;
    render_dirty_ = true;
  }

  void acknowledgeError() {
    state_ = previous_state_;
    if (state_ == DisplayState::Error) {
      state_ = DisplayState::Initial;
    }
    error_message_ = "";
    render_dirty_ = true;
  }

  void addFeedMessage(const String& message) {
    if (message_count_ < DeviceSettings::kMaxFeedMessages) {
      message_feed_[message_count_] = message;
      message_count_++;
    } else {
      for (size_t i = 1; i < DeviceSettings::kMaxFeedMessages; ++i) {
        message_feed_[i - 1] = message_feed_[i];
      }
      message_feed_[DeviceSettings::kMaxFeedMessages - 1] = message;
    }

    if (message_count_ > 0) {
      selected_message_index_ = message_count_ - 1;
      selected_text_offset_ = 0;
    }
  }

  void maybeCommitPendingMultiTap(uint32_t nowMs) {
    if (pending_key_ == KeyInput::None) {
      return;
    }

    if ((nowMs - pending_updated_ms_) >= DeviceSettings::kMultiTapTimeoutMs) {
      pending_key_ = KeyInput::None;
      pending_char_index_ = 0;
      pending_char_pos_ = -1;
    }
  }

  void commitPendingMultiTap() {
    pending_key_ = KeyInput::None;
    pending_char_index_ = 0;
    pending_char_pos_ = -1;
  }

  void pushMultiTapChar(KeyInput key) {
    const char* chars = multiTapChars(key);
    const size_t chars_len = strlen(chars);
    if (chars_len == 0) {
      return;
    }

    const uint32_t now = millis();
    if (pending_key_ == key && (now - pending_updated_ms_) < DeviceSettings::kMultiTapTimeoutMs && pending_char_pos_ >= 0 &&
        pending_char_pos_ < static_cast<int32_t>(compose_draft_.length())) {
      pending_char_index_ = static_cast<uint8_t>((pending_char_index_ + 1) % chars_len);
      compose_draft_.setCharAt(static_cast<unsigned int>(pending_char_pos_), chars[pending_char_index_]);
      pending_updated_ms_ = now;
      return;
    }

    commitPendingMultiTap();
    if (compose_draft_.length() >= DeviceSettings::kMaxDraftLength) {
      return;
    }

    pending_key_ = key;
    pending_char_index_ = 0;
    compose_draft_ += chars[pending_char_index_];
    pending_char_pos_ = compose_draft_.length() - 1;
    pending_updated_ms_ = now;
  }

  void handleComposeKey(KeyInput key) {
    if (isNumericKey(key)) {
      pushMultiTapChar(key);
      return;
    }

    if (key == KeyInput::B) {
      commitPendingMultiTap();
      return;
    }

    if (key == KeyInput::D) {
      if (pending_key_ != KeyInput::None && pending_char_pos_ >= 0 &&
          pending_char_pos_ < static_cast<int32_t>(compose_draft_.length())) {
        compose_draft_.remove(static_cast<unsigned int>(pending_char_pos_), 1);
        commitPendingMultiTap();
        return;
      }

      if (compose_draft_.length() > 0) {
        compose_draft_.remove(compose_draft_.length() - 1);
      }
      return;
    }

    if (key == KeyInput::A) {
      commitPendingMultiTap();
      state_ = DisplayState::Menu;
      return;
    }

    if (key == KeyInput::C) {
      commitPendingMultiTap();

      if (compose_draft_.length() == 0) {
        return;
      }

      if (!radio_.sendMessage(compose_draft_, 1)) {
        enterError("msg send fail");
        return;
      }

      addFeedMessage("TX:" + compose_draft_);
      compose_draft_ = "";
      state_ = DisplayState::Initial;
      return;
    }
  }

  void handleKey(KeyInput key) {
    if (key == KeyInput::Sys) {
      enterError("system input");
      return;
    }

    if (state_ == DisplayState::Error) {
      if (key == KeyInput::Ack || key == KeyInput::A) {
        acknowledgeError();
      }
      return;
    }

    switch (state_) {
      case DisplayState::Initial:
        if (key == KeyInput::A) {
          state_ = DisplayState::Menu;
          return;
        }
        if (key == KeyInput::Star && message_count_ > 0 && selected_message_index_ > 0) {
          selected_message_index_--;
          selected_text_offset_ = 0;
          return;
        }
        if (key == KeyInput::Hash && message_count_ > 0 && selected_message_index_ + 1 < message_count_) {
          selected_message_index_++;
          selected_text_offset_ = 0;
          return;
        }
        if (key == KeyInput::B && selected_text_offset_ > 0) {
          selected_text_offset_--;
          return;
        }
        if (key == KeyInput::C && message_count_ > 0) {
          const String& msg = message_feed_[selected_message_index_];
          if (selected_text_offset_ + 1 < msg.length()) {
            selected_text_offset_++;
          }
          return;
        }
        return;

      case DisplayState::Menu:
        if (key == KeyInput::A) {
          state_ = DisplayState::Initial;
          return;
        }
        if (key == KeyInput::B) {
          state_ = DisplayState::Compose;
          return;
        }
        return;

      case DisplayState::Compose:
        handleComposeKey(key);
        return;

      case DisplayState::Error:
        return;
    }
  }

  DisplayFrame currentFrame() {
    switch (state_) {
      case DisplayState::Initial:
        return buildInitialFrame(
            message_feed_,
            message_count_,
            selected_message_index_,
            selected_text_offset_,
            display_.width());

      case DisplayState::Menu:
        return buildMenuFrame(display_.width());

      case DisplayState::Compose: {
        size_t offset = 0;
        if (compose_draft_.length() > display_.width()) {
          offset = compose_draft_.length() - display_.width();
        }
        return buildComposeFrame(compose_draft_, offset, display_.width());
      }

      case DisplayState::Error:
        return buildErrorFrame(error_message_, display_.width());
    }

    return buildErrorFrame("invalid state", display_.width());
  }

  void render() {
    const DisplayFrame frame = currentFrame();
    if (frame.line1 == last_rendered_frame_.line1 && frame.line2 == last_rendered_frame_.line2) {
      return;
    }

    display_.renderFrame(frame);
    last_rendered_frame_ = frame;

    // Serial mirror is useful for bring-up when using a non-physical display adapter.
    Serial.print("[LCD] ");
    Serial.print(frame.line1);
    Serial.print(" | ");
    Serial.println(frame.line2);
  }

  DisplayPort& display_;
  ButtonPanelPort& buttons_;
  RadioPort& radio_;
  AlertDetectionPort& alert_detection_;

  DisplayState state_;
  DisplayState previous_state_;
  String error_message_;

  String message_feed_[DeviceSettings::kMaxFeedMessages];
  size_t message_count_;
  size_t selected_message_index_;
  size_t selected_text_offset_;

  String compose_draft_;
  KeyInput pending_key_;
  uint8_t pending_char_index_;
  int32_t pending_char_pos_;
  uint32_t pending_updated_ms_;

  String alert_queue_[DeviceSettings::kMaxQueuedAlerts];
  size_t alert_queue_count_;

  uint32_t last_display_render_ms_;
  uint32_t last_alert_poll_ms_;
  bool render_dirty_;
  DisplayFrame last_rendered_frame_;
};

MainDevice* MainDevice::instance_ = nullptr;

MockDisplayAdapter g_display(16, 2);
MatrixButtonPanelAdapter g_buttons(
  DeviceSettings::kButtonRows,
  DeviceSettings::kButtonCols,
  DeviceSettings::kButtonDebounceMs);

#if defined(PIPSURVIVOR_RADIO_ESPNOW)
EspNowRadioAdapter g_radio_impl(DeviceSettings::buildRadioConfig(), 0);
#elif defined(PIPSURVIVOR_RADIO_RYLR998)
Rylr998RadioAdapter g_radio_impl(DeviceSettings::buildRadioConfig(), Serial2, 1, 115200);
#else
MockRadioAdapter g_radio_impl(DeviceSettings::buildRadioConfig());
#endif

RadioPort& g_radio = g_radio_impl;

Mpu6050GyroscopeAdapter g_gyro;
GyroscopeJerkAdapter g_jerk(g_gyro);

Bmp280BarometerAdapter g_bmp;
BarometerAltitudeAdapter g_altitude(g_bmp);

TztWaterLevelAdapter g_water_sensor(
  DeviceSettings::kWaterSensorAnalogPin,
  DeviceSettings::kWaterSensorDigitalPin,
  DeviceSettings::kWaterSensorPowerPin,
  DeviceSettings::kWaterSensorDryRaw,
  DeviceSettings::kWaterSensorWetRaw,
  DeviceSettings::kWaterSensorDigitalWetHigh,
  DeviceSettings::kWaterSensorWetThresholdRaw,
  DeviceSettings::kWaterSensorSettleDelayMs);
WaterLevelSubmersionAdapter g_submersion(g_water_sensor, DeviceSettings::buildSubmersionConfig());

AlertDetectionAdapter g_alert_detection(
  g_jerk,
  g_altitude,
  g_submersion,
  DeviceSettings::buildAlertMetaParams());

MainDevice g_device(g_display, g_buttons, g_radio, g_alert_detection);

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  // Initialize the specific I2C bus pins required by this board configuration.
  Wire.begin(DeviceSettings::kI2cPinSda, DeviceSettings::kI2cPinScl);

#if defined(PIPSURVIVOR_RADIO_ESPNOW)
  g_radio_impl.begin();
#elif defined(PIPSURVIVOR_RADIO_RYLR998)
  Serial2.begin(115200, SERIAL_8N1, DeviceSettings::kRadioRxPin, DeviceSettings::kRadioTxPin);
#endif

  MainDevice::setInstance(&g_device);

  Serial.println("[MAIN_DEVICE] init");
  const bool ok = g_device.begin();
  Serial.print("[MAIN_DEVICE] begin status: ");
  Serial.println(ok ? "OK" : "PARTIAL/FAIL");
}

void loop() {
  g_device.loop();
}