#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>

#include "adapters/alert_detection_adapter.h"
#include "adapters/barometer_altitude_adapter.h"
#include "adapters/bmp280_barometer_adapter.h"
#include "adapters/dual_radio_adapter.h"
#include "adapters/gyroscope_jerk_adapter.h"
#include "adapters/matrix_button_panel_adapter.h"
#include "adapters/mock_display_adapter.h"
#include "adapters/mock_radio_adapter.h"
#include "adapters/espnow_radio_adapter.h"
#include "adapters/mpu6050_accel_adapter.h"
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
    case AlertSensorMetric::Submersion:
      return "submerged";
    case AlertSensorMetric::FallRapid:
      return "fall";
    case AlertSensorMetric::OrientationFlip:
      return "flip";
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
      return "2ABC";
    case KeyInput::K3:
      return "3DEF";
    case KeyInput::K4:
      return "4GHI";
    case KeyInput::K5:
      return "5JKL";
    case KeyInput::K6:
      return "6MNO";
    case KeyInput::K7:
      return "7PQRS";
    case KeyInput::K8:
      return "8TUV";
    case KeyInput::K9:
      return "9WXYZ";
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
         alert_queue_count_(0),
         alert_count_(0),
         selected_alert_index_(0),
         last_display_render_ms_(0),
        last_alert_poll_ms_(0),
        last_ping_ms_(0),
        ping_count_(0),
        pending_key_(KeyInput::None),
        pending_char_index_(0),
        pending_char_pos_(-1),
        pending_updated_ms_(0),
        render_dirty_(true),
        last_rendered_frame_(DisplayFrame{"", ""}) {
    feed_mutex_ = xSemaphoreCreateRecursiveMutex();
    radio_mutex_ = xSemaphoreCreateMutex();
  }

  static void setInstance(MainDevice* instance) {
    instance_ = instance;
  }

  static MainDevice* instance() {
    return instance_;
  }

  size_t getMessageCount() const {
    xSemaphoreTakeRecursive(feed_mutex_, portMAX_DELAY);
    size_t count = message_count_;
    xSemaphoreGiveRecursive(feed_mutex_);
    return count;
  }

  DisplayFrame getLastRenderedFrame() const {
    xSemaphoreTakeRecursive((SemaphoreHandle_t)feed_mutex_, portMAX_DELAY);
    DisplayFrame frame = last_rendered_frame_;
    xSemaphoreGiveRecursive((SemaphoreHandle_t)feed_mutex_);
    return frame;
  }
  
  String getMessage(size_t index) const {
    xSemaphoreTakeRecursive(feed_mutex_, portMAX_DELAY);
    String msg = "";
    if (index < message_count_) msg = message_feed_[index];
    xSemaphoreGiveRecursive(feed_mutex_);
    return msg;
  }

  bool begin() {
    display_.clear();

    const bool buttons_ok = DeviceSettings::kEnableButtons ? buttons_.begin() : true;
    const bool alert_ok = DeviceSettings::kEnableAlertDetection ? alert_detection_.begin() : true;

    radio_.setReceiveCallback(MainDevice::onRadioMessageStatic);

    if (DeviceSettings::kEnableAlertDetection) {
      AlertDetectionCallbackParams cb{};
      cb.source = "main_device";
      cb.channel = "radio-alert";
      cb.severity = 2;
      alert_detection_.setCallback(MainDevice::onAlertStatic, cb);
    }

    addFeedMessage("System ready");
    render_dirty_ = true;

    return buttons_ok && alert_ok;
  }

  void loop() {
    if (DeviceSettings::kEnableAlertDetection) {
      const uint32_t now = millis();
      if ((now - last_alert_poll_ms_) >= DeviceSettings::kAlertPollMs) {
        AlertDetectionReading reading{};
        alert_detection_.poll(reading);
        last_alert_poll_ms_ = now;

        const AlertDetectionReading& s = alert_detection_.latestReading();
        Serial.print("[SENSOR] jerk=");
        Serial.print(s.has_jerk ? s.jerk_magnitude : -1.0f, 2);
        Serial.print(" alt_drop=not_tracked");
        Serial.print(" subm=");
        Serial.print(s.has_submersion ? (s.is_submerged ? "YES" : "NO") : "-");
        Serial.print(" norm=");
        Serial.print(s.submersion_normalized, 3);
        Serial.print(" dur=");
        Serial.print(s.submersion_duration_ms);
        Serial.print(" accel=");
        Serial.print(s.has_accel ? s.accel_magnitude : -1.0f, 2);
        Serial.print(" flat=");
        Serial.println(s.is_flat ? "YES" : "NO");
      }
    }




    xSemaphoreTakeRecursive(feed_mutex_, portMAX_DELAY);
    processQueuedAlerts();

    const uint32_t now = millis();
    maybeCommitPendingMultiTap(now);

    if (DeviceSettings::kEnableButtons) {
      KeyInput key = KeyInput::None;
      if (buttons_.readKey(key)) {
        Serial.print("[BTN] key=");
        Serial.println(static_cast<int>(key));
        handleKey(key);
        render_dirty_ = true;
      }
    }

    if (render_dirty_ || (now - last_display_render_ms_) >= DeviceSettings::kDisplayRefreshMs) {
      render();
      last_display_render_ms_ = now;
      render_dirty_ = false;
    }

    xSemaphoreGiveRecursive(feed_mutex_);
  }

  uint32_t getUptime() const { return millis() / 1000; }

 private:
  static MainDevice* instance_;

  static void onRadioMessageStatic(const String& message, uint8_t hops, uint32_t msg_id) {
    if (instance_ != nullptr) {
      instance_->onRadioMessage(message, hops);
    }
  }

  static void onAlertStatic(
      const AlertDetectionEvent& event,
      const AlertDetectionCallbackParams& callbackParams) {
    if (instance_ != nullptr) {
      instance_->onAlert(event, callbackParams);
    }
  }

  void onRadioMessage(const String& message, uint8_t hops) {
    String display = message + " HOP:" + String(hops);
    Serial.println("[RADIO_RX] " + display);
    addFeedMessage("RX:" + display);
    render_dirty_ = true;
  }

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
    Serial.println("[ALERT] " + payload);
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
    addAlertMessage(alert);
    
    Serial.println("[ALERT_TX] " + alert);

    xSemaphoreTake(radio_mutex_, portMAX_DELAY);
    bool ok = radio_.sendAlert(alert, DeviceSettings::kMeshMaxHops);
    xSemaphoreGive(radio_mutex_);
    
    if (!ok) {
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
    xSemaphoreTakeRecursive(feed_mutex_, portMAX_DELAY);
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
    }
    xSemaphoreGiveRecursive(feed_mutex_);
  }

  void addAlertMessage(const String& message) {
    xSemaphoreTakeRecursive(feed_mutex_, portMAX_DELAY);
    if (alert_count_ < DeviceSettings::kMaxFeedMessages) {
      alert_feed_[alert_count_] = message;
      alert_count_++;
    } else {
      for (size_t i = 1; i < DeviceSettings::kMaxFeedMessages; ++i) {
        alert_feed_[i - 1] = alert_feed_[i];
      }
      alert_feed_[DeviceSettings::kMaxFeedMessages - 1] = message;
    }

    if (alert_count_ > 0) {
      selected_alert_index_ = alert_count_ - 1;
    }
    xSemaphoreGiveRecursive(feed_mutex_);
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

    if (key == KeyInput::C || key == KeyInput::Hash) {
      commitPendingMultiTap();

      if (compose_draft_.length() == 0) {
        return;
      }

      xSemaphoreTake(radio_mutex_, portMAX_DELAY);
      bool ok = radio_.sendMessage(compose_draft_, DeviceSettings::kMeshMaxHops);
      xSemaphoreGive(radio_mutex_);

      if (!ok) {
        enterError("msg send fail");
        return;
      }

      Serial.println("[RADIO_TX] " + compose_draft_);
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

    const DisplayState oldState = state_;
    if (state_ == DisplayState::Error) {
      if (key == KeyInput::Ack || key == KeyInput::A) {
        acknowledgeError();
        Serial.printf("[STATE] Error -> %d (ACK)\n", static_cast<int>(state_));
      }
      return;
    }

    switch (state_) {
      case DisplayState::Initial:
        if (key == KeyInput::A) {
          state_ = DisplayState::Menu;
          return;
        }
        if (key == KeyInput::B && message_count_ > 0 && selected_message_index_ > 0) {
          selected_message_index_--;
          return;
        }
        if (key == KeyInput::C && message_count_ > 0 && selected_message_index_ + 1 < message_count_) {
          selected_message_index_++;
          return;
        }

        return;

      case DisplayState::Menu:
        if (key == KeyInput::A || key == KeyInput::Hash) {
          state_ = DisplayState::Initial;
          return;
        }
        if (key == KeyInput::K1) {
          state_ = DisplayState::Compose;
          return;
        }
        if (key == KeyInput::K2) {
          state_ = DisplayState::ViewAlerts;
          return;
        }
        if (key == KeyInput::K3) {
          state_ = DisplayState::Initial;
          return;
        }
        return;

      case DisplayState::ViewAlerts:
        if (key == KeyInput::A) {
          state_ = DisplayState::Menu;
          return;
        }
        if (key == KeyInput::B && alert_count_ > 0 && selected_alert_index_ > 0) {
          selected_alert_index_--;
          return;
        }
        if (key == KeyInput::C && alert_count_ > 0 && selected_alert_index_ + 1 < alert_count_) {
          selected_alert_index_++;
          return;
        }

        return;

      case DisplayState::Compose:
        if (key == KeyInput::Star || key == KeyInput::Hash) {
          return;
        }
        handleComposeKey(key);
        return;

      case DisplayState::Error:
        return;
    }
    if (state_ != oldState) {
      Serial.printf("[STATE] %d -> %d\n", static_cast<int>(oldState), static_cast<int>(state_));
    }
  }

  DisplayFrame currentFrame() {
    DisplayFrame f{"", "", "", ""};
    switch (state_) {
      case DisplayState::Initial: {
        String content = "Inbox Empty";
        String meta = "Up:" + String(millis()/1000) + "s";
        if (message_count_ > 0) {
          const String& msg = message_feed_[selected_message_index_];
          uint8_t dw = display_.width();
          if (msg.length() > dw) {
            size_t maxOff = msg.length() - dw;
            float phase = fmodf(millis() / 1000.0f * 0.15f, 1.0f);
            float tri = phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f;
            size_t off = static_cast<size_t>(tri * maxOff);
            content = msg.substring(off, off + dw);
          } else {
            content = msg;
          }
          meta = "Msg " + String(selected_message_index_ + 1) + "/" + String(message_count_);
        }
        f.line1 = content;
        f.line2 = meta;
        f.hint  = "A:Menu B:Up C:Dn";
        break;
      }

      case DisplayState::Menu:
        f.line1 = "1) mk msg  [MM]";
        f.line2 = "2) view alerts";
        f.line3 = "3) view msgs";
        return f;

      case DisplayState::ViewAlerts: {
        String content = "No Alerts";
        String meta = "";
        if (alert_count_ > 0) {
          const String& alert = alert_feed_[selected_alert_index_];
          uint8_t dw = display_.width();
          if (alert.length() > dw) {
            size_t maxOff = alert.length() - dw;
            float phase = fmodf(millis() / 1000.0f * 0.15f, 1.0f);
            float tri = phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f;
            size_t off = static_cast<size_t>(tri * maxOff);
            content = alert.substring(off, off + dw);
          } else {
            content = alert;
          }
          meta = "Alert " + String(selected_alert_index_ + 1) + "/" + String(alert_count_);
        }
        f.line1 = content;
        f.line2 = meta;
        f.hint  = "A:Menu B:Up C:Dn";
        break;
      }

      case DisplayState::Compose: {
        String full = "Typing: " + compose_draft_;
        uint8_t dw = display_.width();
        if (full.length() > dw) {
          size_t maxOff = full.length() - dw;
          float phase = fmodf(millis() / 1000.0f * 0.15f, 1.0f);
          float tri = phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f;
          size_t off = static_cast<size_t>(tri * maxOff);
          f.line1 = full.substring(off, off + dw);
        } else {
          f.line1 = full;
        }
        f.line2 = "Len: " + String(compose_draft_.length()) + "/160";
        f.hint  = "A:BCK C:SND D:BS";
        break;
      }

      case DisplayState::Error:
        f.line1 = "!! ERROR !!";
        f.line2 = error_message_;
        f.hint  = "A:ACKNOWLEDGE";
        break;
    }

    f.line3 = f.hint;
    return f;
  }

  void render() {
    const DisplayFrame frame = currentFrame();
    if (frame.line1 == last_rendered_frame_.line1 && 
        frame.line2 == last_rendered_frame_.line2 && 
        frame.line3 == last_rendered_frame_.line3) {
      return;
    }

    display_.renderFrame(frame);
    last_rendered_frame_ = frame;

    Serial.print("[LCD] L1:");
    Serial.print(frame.line1);
    Serial.print(" | L2:");
    Serial.print(frame.line2);
    Serial.print(" | L3:");
    Serial.println(frame.line3);
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

  String compose_draft_;
  KeyInput pending_key_;
  uint8_t pending_char_index_;
  int32_t pending_char_pos_;
  uint32_t pending_updated_ms_;

  String alert_queue_[DeviceSettings::kMaxQueuedAlerts];
  size_t alert_queue_count_;

  String alert_feed_[DeviceSettings::kMaxFeedMessages];
  size_t alert_count_;
  size_t selected_alert_index_;

  uint32_t last_display_render_ms_;
  uint32_t last_alert_poll_ms_;
  uint32_t last_ping_ms_;
  uint16_t ping_count_;
  bool render_dirty_;
  DisplayFrame last_rendered_frame_;

 public:
  SemaphoreHandle_t feed_mutex_;
  SemaphoreHandle_t radio_mutex_;
};

MainDevice* MainDevice::instance_ = nullptr;

class WebButtonAdapter : public ButtonPanelPort {
 public:
  WebButtonAdapter() : head_(0), tail_(0) {
    mutex_ = xSemaphoreCreateMutex();
  }

  bool begin() override { return true; }

  bool readKey(KeyInput& key) override {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    bool has_key = false;
    if (head_ != tail_) {
      key = queue_[head_];
      head_ = (head_ + 1) % 16;
      has_key = true;
    }
    xSemaphoreGive(mutex_);
    return has_key;
  }

  void pushKey(KeyInput key) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    uint8_t next_tail = (tail_ + 1) % 16;
    if (next_tail != head_) {
      queue_[tail_] = key;
      tail_ = next_tail;
    }
    xSemaphoreGive(mutex_);
  }

 private:
  KeyInput queue_[16];
  uint8_t head_;
  uint8_t tail_;
  SemaphoreHandle_t mutex_;
};

class CompositeButtonAdapter : public ButtonPanelPort {
 public:
  CompositeButtonAdapter(ButtonPanelPort& a, ButtonPanelPort& b) : a_(a), b_(b) {}
  bool begin() override {
    a_.begin();
    b_.begin();
    return true;
  }
  bool readKey(KeyInput& key) override {
    if (a_.readKey(key)) return true;
    if (b_.readKey(key)) return true;
    return false;
  }
 private:
  ButtonPanelPort& a_;
  ButtonPanelPort& b_;
};



MockDisplayAdapter g_display(16, 2);
MatrixButtonPanelAdapter g_physical_buttons(
  DeviceSettings::kButtonRows,
  DeviceSettings::kButtonCols,
  DeviceSettings::kButtonDebounceMs);

WebButtonAdapter g_web_buttons;
CompositeButtonAdapter g_buttons(g_physical_buttons, g_web_buttons);

#if defined(PIPSURVIVOR_RADIO_DUAL)
  EspNowRadioAdapter g_espnow_impl(DeviceSettings::buildRadioConfig(), DeviceSettings::kEspNowChannel);
  Rylr998RadioAdapter g_rylr998_impl(DeviceSettings::buildRadioConfig(), Serial2, DeviceSettings::kMeshBroadcastAddress, 115200);
  DualRadioAdapter g_radio_impl(g_rylr998_impl, g_espnow_impl);
#elif defined(PIPSURVIVOR_RADIO_ESPNOW)
  EspNowRadioAdapter g_radio_impl(DeviceSettings::buildRadioConfig(), 0);
#elif defined(PIPSURVIVOR_RADIO_RYLR998)
  Rylr998RadioAdapter g_radio_impl(DeviceSettings::buildRadioConfig(), Serial2, DeviceSettings::kMeshBroadcastAddress, 115200);
#else
  MockRadioAdapter g_radio_impl(DeviceSettings::buildRadioConfig());
#endif

RadioPort& g_radio = g_radio_impl;

Mpu6050GyroscopeAdapter g_gyro;
Mpu6050AccelAdapter g_accel;
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
  g_accel,
  DeviceSettings::buildAlertMetaParams());

MainDevice g_device(g_display, g_buttons, g_radio, g_alert_detection);

WebServer g_web_server(80);

void handleRoot() {
  Serial.println("[HTTP] GET / (root page)");
  String html = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>PipSurvivor 16x3</title>
  <style>
    :root {
      --bg: #0b0f19;
      --lcd-bg: #8bac0f;
      --lcd-text: #0f380f;
    }
    body {
      background-color: var(--bg);
      margin: 0;
      height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      font-family: system-ui, sans-serif;
    }
    .container { width: 100%; max-width: 400px; padding: 1rem; }
    .lcd {
      background: #111;
      padding: 15px;
      border-radius: 10px;
      border: 4px solid #333;
      margin-bottom: 2rem;
      box-shadow: 0 10px 30px rgba(0,0,0,0.8);
    }
    .lcd-screen {
      background: var(--lcd-bg);
      color: var(--lcd-text);
      font-family: 'Courier New', monospace;
      padding: 15px;
      border-radius: 4px;
      font-size: 1.6rem;
      line-height: 1.3;
      white-space: pre;
      font-weight: 900;
      box-shadow: inset 0 0 15px rgba(0,0,0,0.5);
    }
    .line { height: 1.8rem; overflow: hidden; }
    .keypad {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 12px;
      background: #1a1a2e;
      padding: 20px;
      border-radius: 20px;
      border: 4px solid #2a2a40;
    }
    .key {
      aspect-ratio: 1;
      border: none;
      border-radius: 10px;
      color: #fff;
      cursor: pointer;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      box-shadow: 0 5px 0 #000;
    }
    .key:active { transform: translateY(4px); box-shadow: none; }
    .key.n { background: #313244; }
    .key.f { background: #f38ba8; }
    .km { font-size: 1.8rem; font-weight: 800; }
    .ks { font-size: 0.6rem; opacity: 0.8; }
  </style>
  <script>
    function pk(k) { fetch('/key?k=' + k); }
    async function u() {
      try {
        const r = await fetch('/lcd');
        const d = await r.json();
        document.getElementById('l1').textContent = (d.l1 + "                ").substring(0, 16);
        document.getElementById('l2').textContent = (d.l2 + "                ").substring(0, 16);
        document.getElementById('l3').textContent = (d.l3 + "                ").substring(0, 16);
      } catch(e) {}
    }
    setInterval(u, 300);
  </script>
</head>
<body>
  <div class="container">
    <div class="lcd">
      <div class="lcd-screen">
        <div id="l1" class="line"></div>
        <div id="l2" class="line"></div>
        <div id="l3" class="line"></div>
      </div>
    </div>
    <div class="keypad">
      <button class="key n" onclick="pk('K1')"><div class="km">1</div><div class="ks">.,!?</div></button>
      <button class="key n" onclick="pk('K2')"><div class="km">2</div><div class="ks">ABC</div></button>
      <button class="key n" onclick="pk('K3')"><div class="km">3</div><div class="ks">DEF</div></button>
      <button class="key f" onclick="pk('A')"><div class="km">A</div><div class="ks">MENU</div></button>
      <button class="key n" onclick="pk('K4')"><div class="km">4</div><div class="ks">GHI</div></button>
      <button class="key n" onclick="pk('K5')"><div class="km">5</div><div class="ks">JKL</div></button>
      <button class="key n" onclick="pk('K6')"><div class="km">6</div><div class="ks">MNO</div></button>
      <button class="key f" onclick="pk('B')"><div class="km">B</div><div class="ks">UP</div></button>
      <button class="key n" onclick="pk('K7')"><div class="km">7</div><div class="ks">PQRS</div></button>
      <button class="key n" onclick="pk('K8')"><div class="km">8</div><div class="ks">TUV</div></button>
      <button class="key n" onclick="pk('K9')"><div class="km">9</div><div class="ks">WXYZ</div></button>
      <button class="key f" onclick="pk('C')"><div class="km">C</div><div class="ks">DOWN</div></button>
      <button class="key f" onclick="pk('Star')"><div class="km">*</div><div class="ks">LEFT</div></button>
      <button class="key n" onclick="pk('K0')"><div class="km">0</div><div class="ks">_</div></button>
      <button class="key f" onclick="pk('Hash')"><div class="km">#</div><div class="ks">RIGHT</div></button>
      <button class="key f" onclick="pk('D')"><div class="km">D</div><div class="ks">DEL</div></button>
    </div>
  </div>
</body>
</html>
)HTML";
  g_web_server.send(200, "text/html", html);
}


void backgroundReceiverTask(void* parameter) {
  for (;;) {
    if (DeviceSettings::kEnableRadio && MainDevice::instance() != nullptr) {
      xSemaphoreTake(MainDevice::instance()->radio_mutex_, portMAX_DELAY);
      g_radio.poll();
      xSemaphoreGive(MainDevice::instance()->radio_mutex_);
    }

    if (DeviceSettings::kEnableWebServer) {
      g_web_server.handleClient();
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void handleKeyInput() {
  if (g_web_server.hasArg("k")) {
    String k = g_web_server.arg("k");
    KeyInput key = KeyInput::None;
    if (k == "K1") key = KeyInput::K1;
    else if (k == "K2") key = KeyInput::K2;
    else if (k == "K3") key = KeyInput::K3;
    else if (k == "K4") key = KeyInput::K4;
    else if (k == "K5") key = KeyInput::K5;
    else if (k == "K6") key = KeyInput::K6;
    else if (k == "K7") key = KeyInput::K7;
    else if (k == "K8") key = KeyInput::K8;
    else if (k == "K9") key = KeyInput::K9;
    else if (k == "K0") key = KeyInput::K0;
    else if (k == "A") key = KeyInput::A;
    else if (k == "B") key = KeyInput::B;
    else if (k == "C") key = KeyInput::C;
    else if (k == "D") key = KeyInput::D;
    else if (k == "Star") key = KeyInput::Star;
    else if (k == "Hash") key = KeyInput::Hash;
    
    if (key != KeyInput::None) {
      Serial.printf("[HTTP] Key Input: %s\n", k.c_str());
      g_web_buttons.pushKey(key);
    }
  }
  g_web_server.send(200, "text/plain", "OK");
}

void handleFeed() {
  g_web_server.send(404, "text/plain", "Removed");
}

void handleLcd() {
  DisplayFrame frame = g_device.getLastRenderedFrame();
  
  auto escape = [](String s) {
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    return s;
  };

  String json = "{";
  json += "\"l1\":\"" + escape(frame.line1) + "\",";
  json += "\"l2\":\"" + escape(frame.line2) + "\",";
  json += "\"l3\":\"" + escape(frame.line3) + "\"";
  json += "}";
  
  Serial.println("[HTTP] /lcd -> " + json);
  g_web_server.send(200, "application/json", json);
}

void handleMesh() {
  if (MainDevice::instance() == nullptr) {
    g_web_server.send(503, "text/plain", "Device not ready");
    return;
  }

  if (xSemaphoreTake(MainDevice::instance()->radio_mutex_, pdMS_TO_TICKS(50)) == pdFALSE) {
    g_web_server.send(503, "text/plain", "Busy");
    return;
  }

  MeshMessageEntry entries[DeviceSettings::kMeshHistorySize];
  size_t count = 0;
  uint32_t total_rx = 0;
  size_t cache_size = 0;
  size_t relay_jobs = 0;

  g_radio.getRecentMessages(entries, DeviceSettings::kMeshHistorySize, count);
  g_radio.getMeshStats(total_rx, cache_size, relay_jobs);

  uint32_t my_uid = g_radio.getDeviceUid();

  bool espnow_tx = false;
  bool rylr998_tx = false;
#if defined(PIPSURVIVOR_RADIO_DUAL)
  DualRadioAdapter* dual = static_cast<DualRadioAdapter*>(&g_radio);
  espnow_tx = dual->isEspNowTxEnabled();
  rylr998_tx = dual->isRylr998TxEnabled();
#endif

  xSemaphoreGive(MainDevice::instance()->radio_mutex_);

  auto escape = [](String s) {
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    s.replace("\n", "\\n");
    s.replace("\r", "\\r");
    return s;
  };

  String json = "{";
  json += "\"my_uid\":\"" + String(my_uid, HEX) + "\",";
#if defined(PIPSURVIVOR_RADIO_DUAL)
  json += "\"espnow_tx\":" + String(espnow_tx ? "true" : "false") + ",";
  json += "\"rylr998_tx\":" + String(rylr998_tx ? "true" : "false") + ",";
#endif
  json += "\"messages\":[";

  uint32_t now = millis();
  for (size_t i = 0; i < count; i++) {
    if (i > 0) json += ",";
    json += "{\"sender\":\"" + String(entries[i].sender_uid, HEX) + "\",";
    json += "\"msg_id\":" + String(entries[i].msg_id) + ",";
    json += "\"ttl\":" + String(entries[i].ttl) + ",";
    json += "\"payload\":\"" + escape(entries[i].payload) + "\",";
    json += "\"age_ms\":" + String(now - entries[i].recv_time_ms) + "}";
  }

  json += "],\"stats\":{";
  json += "\"total_rx\":" + String(total_rx) + ",";
  json += "\"cache_size\":" + String(cache_size) + ",";
  json += "\"relay_jobs\":" + String(relay_jobs) + "}}";

  Serial.println("[HTTP] /mesh -> " + json);
  g_web_server.send(200, "application/json", json);
}

void initWebServer() {
  if (!DeviceSettings::kEnableWebServer) return;

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ssidBuffer[32];
  snprintf(ssidBuffer, sizeof(ssidBuffer), "%s-%02X%02X", DeviceSettings::kApSsid, mac[4], mac[5]);

  Serial.print("[WIFI] Starting AP: ");
  Serial.println(ssidBuffer);
  WiFi.softAP(ssidBuffer, DeviceSettings::kApPassword);
  
  g_web_server.on("/", handleRoot);
  g_web_server.on("/key", handleKeyInput);
  g_web_server.on("/feed", handleFeed);
  g_web_server.on("/lcd", handleLcd);
  g_web_server.on("/mesh", handleMesh);
  g_web_server.begin();
  Serial.print("[WIFI] AP started. Root IP: ");
  Serial.println(WiFi.softAPIP());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("[BOOT] PipSurvivor starting...");
  Serial.println(DeviceSettings::kEnableRadio         ? "[BOOT] radio:       ON"  : "[BOOT] radio:       OFF");
  Serial.println(DeviceSettings::kEnableGyroscope     ? "[BOOT] gyroscope:   ON"  : "[BOOT] gyroscope:   OFF");
  Serial.println(DeviceSettings::kEnableBarometer     ? "[BOOT] barometer:   ON"  : "[BOOT] barometer:   OFF");
  Serial.println(DeviceSettings::kEnableWaterSensor   ? "[BOOT] water:       ON"  : "[BOOT] water:       OFF");
  Serial.println(DeviceSettings::kEnableButtons       ? "[BOOT] buttons:     ON"  : "[BOOT] buttons:     OFF");
  Serial.println(DeviceSettings::kEnableDisplay       ? "[BOOT] display:     ON"  : "[BOOT] display:     OFF");

  // I2C is only needed when at least one I2C sensor is enabled.
  if (DeviceSettings::kEnableGyroscope || DeviceSettings::kEnableBarometer) {
    Wire.begin(DeviceSettings::kI2cPinSda, DeviceSettings::kI2cPinScl);
  }

  // Initialize individual sensors guarded by their enable flags.
  if (DeviceSettings::kEnableGyroscope)   { g_gyro.begin(); g_accel.begin(); }
  if (DeviceSettings::kEnableBarometer)   { g_bmp.begin(); }
  if (DeviceSettings::kEnableWaterSensor) { g_water_sensor.begin(); }

#if defined(PIPSURVIVOR_RADIO_DUAL)
  if (DeviceSettings::kEnableRadio) {
    Serial2.begin(115200, SERIAL_8N1, DeviceSettings::kRadioRxPin, DeviceSettings::kRadioTxPin);
    WiFi.mode(WIFI_AP_STA);
    g_radio_impl.begin();
    g_radio_impl.enableEspNow(DeviceSettings::kEnableEspNowTx);
    g_radio_impl.enableRylr998(DeviceSettings::kEnableRylr998Tx);
  }
#elif defined(PIPSURVIVOR_RADIO_ESPNOW)
  if (DeviceSettings::kEnableRadio) g_radio_impl.begin();
#elif defined(PIPSURVIVOR_RADIO_RYLR998)
  if (DeviceSettings::kEnableRadio) {
    Serial2.begin(115200, SERIAL_8N1, DeviceSettings::kRadioRxPin, DeviceSettings::kRadioTxPin);
    g_radio_impl.begin();
  }
#endif

  MainDevice::setInstance(&g_device);

  Serial.println("[MAIN_DEVICE] init");
  const bool ok = g_device.begin();
  Serial.print("[MAIN_DEVICE] begin status: ");
  Serial.println(ok ? "OK" : "PARTIAL/FAIL");

  initWebServer();

  xTaskCreatePinnedToCore(
      backgroundReceiverTask,
      "BgReceiver",
      8192,
      NULL,
      1,
      NULL,
      0); // Run on Core 0
}

void loop() {
  g_device.loop();
}