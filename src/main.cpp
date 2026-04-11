#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>

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
        selected_text_offset_(0),
        alert_queue_count_(0),
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
    Serial.println("[RADIO_RX] " + message);
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
    
    xSemaphoreTake(radio_mutex_, portMAX_DELAY);
    bool ok = radio_.sendAlert(alert, 1);
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
      selected_text_offset_ = 0;
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
      bool ok = radio_.sendMessage(compose_draft_, 1);
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
  uint32_t last_ping_ms_;
  uint16_t ping_count_;
  bool render_dirty_;
  DisplayFrame last_rendered_frame_;

 public:
  SemaphoreHandle_t feed_mutex_;
  SemaphoreHandle_t radio_mutex_;
};

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

MainDevice* MainDevice::instance_ = nullptr;

MockDisplayAdapter g_display(16, 2);
MatrixButtonPanelAdapter g_physical_buttons(
  DeviceSettings::kButtonRows,
  DeviceSettings::kButtonCols,
  DeviceSettings::kButtonDebounceMs);

WebButtonAdapter g_web_buttons;
CompositeButtonAdapter g_buttons(g_physical_buttons, g_web_buttons);

#if defined(PIPSURVIVOR_RADIO_ESPNOW)
EspNowRadioAdapter g_radio_impl(DeviceSettings::buildRadioConfig(), 0);
#elif defined(PIPSURVIVOR_RADIO_RYLR998)
Rylr998RadioAdapter g_radio_impl(DeviceSettings::buildRadioConfig(), Serial2, DeviceSettings::kMeshBroadcastAddress, 115200);
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

WebServer g_web_server(80);

void handleRoot() {
  String html = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>PipSurvivor Receiver</title>
  <style>
    :root {
      --bg: #0f172a;
      --card-bg: #1e293b;
      --text: #f8fafc;
      --accent: #38bdf8;
      --border: #334155;
    }
    body {
      background-color: var(--bg);
      color: var(--text);
      font-family: 'Inter', -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      margin: 0;
      padding: 2rem;
      display: flex;
      justify-content: center;
    }
    .container {
      max-width: 600px;
      width: 100%;
    }
    h1 {
      color: var(--accent);
      text-align: center;
      margin-bottom: 2rem;
      letter-spacing: -0.05em;
    }
    .send-header {
      color: var(--text);
      text-align: center;
      margin-bottom: 1rem;
      font-size: 1.5rem;
    }
    .keypad-container {
      background: #3c3c3c;
      padding: 15px;
      border-radius: 12px;
      border: 3px solid #555;
      margin-bottom: 2rem;
    }
    .keypad {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 12px;
    }
    .key {
      aspect-ratio: 1;
      border: 2px solid transparent;
      border-radius: 8px;
      color: white;
      cursor: pointer;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      transition: filter 0.1s;
      user-select: none;
      padding: 0;
    }
    .key:active { filter: brightness(0.8) !important; }
    .key.blue {
      background: #6f8cf2;
      border-color: #5a7add;
    }
    .key.red {
      background: #d94a4a;
      border-color: #c73939;
    }
    .key-main { font-size: 1.8rem; font-weight: 500; line-height: 1; margin-bottom: 4px; }
    .key-sub { font-size: 0.7rem; opacity: 0.9; line-height: 1; min-height: 0.7rem; font-weight: normal; }
    .messages {
      display: flex;
      flex-direction: column;
      gap: 1rem;
    }
    .message {
      background: var(--card-bg);
      padding: 1.2rem;
      border-radius: 12px;
      border: 1px solid var(--border);
      box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.1);
      font-size: 1.1rem;
      line-height: 1.5;
      animation: fadeIn 0.4s ease-out;
    }
    .rx-badge {
      display: inline-block;
      background: rgba(56, 189, 248, 0.15);
      color: var(--accent);
      padding: 0.2rem 0.6rem;
      border-radius: 4px;
      font-size: 0.8rem;
      font-weight: 600;
      margin-bottom: 0.5rem;
      text-transform: uppercase;
    }
    .tx-badge {
      display: inline-block;
      background: rgba(167, 139, 250, 0.15);
      color: #a78bfa;
      padding: 0.2rem 0.6rem;
      border-radius: 4px;
      font-size: 0.8rem;
      font-weight: 600;
      margin-bottom: 0.5rem;
      text-transform: uppercase;
    }
    .lcd-screen {
      background: #8bac0f;
      color: #0f380f;
      font-family: 'Courier New', Courier, monospace;
      padding: 12px;
      border: 4px solid #111;
      border-radius: 6px;
      margin-bottom: 1rem;
      font-size: 1.4rem;
      line-height: 1.4;
      white-space: pre;
      text-align: left;
      font-weight: bold;
      text-shadow: 1px 1px 0px rgba(139, 172, 15, 0.4);
      box-shadow: inset 0 0 10px rgba(0,0,0,0.5);
    }
    .alr-badge {
      display: inline-block;
      background: rgba(248, 113, 113, 0.15);
      color: #f87171;
      padding: 0.2rem 0.6rem;
      border-radius: 4px;
      font-size: 0.8rem;
      font-weight: 600;
      margin-bottom: 0.5rem;
      text-transform: uppercase;
    }
    @keyframes fadeIn {
      from { opacity: 0; transform: translateY(10px); }
      to { opacity: 1; transform: translateY(0); }
    }
    .empty { text-align: center; color: #94a3b8; padding: 2rem; }
  </style>
  <script>
    function pk(k) {
      fetch('/key?k=' + k).catch(e => console.error(e));
    }
    async function updateFeed() {
      try {
        const r = await fetch('/feed');
        const t = await r.text();
        document.getElementById('msg-list').innerHTML = t;
      } catch(e) {}
    }
    async function updateLcd() {
      try {
        const r = await fetch('/lcd');
        const t = await r.text();
        document.getElementById('lcd-screen').innerHTML = t;
      } catch(e) {}
    }
    setInterval(updateFeed, 3000);
    setInterval(updateLcd, 500);
  </script>
</head>
<body>
  <div class="container">
    <h1 class="send-header">Send Message</h1>
    <div class="keypad-container">
      <div id="lcd-screen" class="lcd-screen">Loading...</div>
      <div class="keypad">
        <button class="key blue" onclick="pk('K1')"><div class="key-main">1</div><div class="key-sub">.,!?</div></button>
        <button class="key blue" onclick="pk('K2')"><div class="key-main">2</div><div class="key-sub">ABC</div></button>
        <button class="key blue" onclick="pk('K3')"><div class="key-main">3</div><div class="key-sub">DEF</div></button>
        <button class="key red" onclick="pk('A')"><div class="key-main">A</div><div class="key-sub"></div></button>

        <button class="key blue" onclick="pk('K4')"><div class="key-main">4</div><div class="key-sub">GHI</div></button>
        <button class="key blue" onclick="pk('K5')"><div class="key-main">5</div><div class="key-sub">JKL</div></button>
        <button class="key blue" onclick="pk('K6')"><div class="key-main">6</div><div class="key-sub">MNO</div></button>
        <button class="key red" onclick="pk('B')"><div class="key-main">B</div><div class="key-sub"></div></button>

        <button class="key blue" onclick="pk('K7')"><div class="key-main">7</div><div class="key-sub">PQRS</div></button>
        <button class="key blue" onclick="pk('K8')"><div class="key-main">8</div><div class="key-sub">TUV</div></button>
        <button class="key blue" onclick="pk('K9')"><div class="key-main">9</div><div class="key-sub">WXYZ</div></button>
        <button class="key red" onclick="pk('C')"><div class="key-main">C</div><div class="key-sub"></div></button>

        <button class="key red" onclick="pk('Star')"><div class="key-main">*</div><div class="key-sub"></div></button>
        <button class="key blue" onclick="pk('K0')"><div class="key-main">0</div><div class="key-sub">[space]</div></button>
        <button class="key red" onclick="pk('Hash')"><div class="key-main">#</div><div class="key-sub"></div></button>
        <button class="key red" onclick="pk('D')"><div class="key-main">D</div><div class="key-sub"></div></button>
      </div>
    </div>
    
    <h1>Receiving Feed</h1>
    <div id="msg-list" class="messages">
)HTML";

  if (g_device.getMessageCount() == 0) {
    html += "<div class='empty'>No messages received yet.</div>";
  } else {
    size_t count = g_device.getMessageCount();
    // Show up to 5 last messages correctly
    size_t start_idx = count >= 5 ? count - 5 : 0;
    for (int i = count - 1; i >= static_cast<int>(start_idx); --i) {
      String msg = g_device.getMessage(i);
      if (msg.startsWith("RX:")) {
        msg = msg.substring(3);
      }
      html += "<div class='message'><div class='rx-badge'>Inbound</div>" + msg + "</div>";
    }
  }

  html += R"(
    </div>
  </div>
</body>
</html>
)";

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
      g_web_buttons.pushKey(key);
    }
  }
  g_web_server.send(200, "text/plain", "OK");
}

void handleFeed() {
  String partial = "";
  if (g_device.getMessageCount() == 0) {
    partial = "<div class='empty'>No messages received yet.</div>";
  } else {
    size_t count = g_device.getMessageCount();
    size_t start_idx = count >= 5 ? count - 5 : 0;
    for (int i = count - 1; i >= static_cast<int>(start_idx); --i) {
      String msg = g_device.getMessage(i);
      String badge = "System";
      String badgeClass = "rx-badge";
      if (msg.startsWith("RX:")) {
        msg = msg.substring(3);
        badge = "Inbound";
      } else if (msg.startsWith("TX:")) {
        msg = msg.substring(3);
        badge = "Outbound";
        badgeClass = "tx-badge";
      } else if (msg.startsWith("ALR[")) {
        badge = "Alert";
        badgeClass = "alr-badge";
      }
      partial += "<div class='message'><div class='" + badgeClass + "'>" + badge + "</div>" + msg + "</div>";
    }
  }
  g_web_server.send(200, "text/html", partial);
}

void handleLcd() {
  DisplayFrame frame = g_device.getLastRenderedFrame();
  String html = frame.line1;
  html.replace(" ", "&nbsp;");
  html += "<br>";
  String l2 = frame.line2;
  l2.replace(" ", "&nbsp;");
  html += l2;
  g_web_server.send(200, "text/html", html);
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
  if (DeviceSettings::kEnableGyroscope)   { g_gyro.begin(); }
  if (DeviceSettings::kEnableBarometer)   { g_bmp.begin(); }
  if (DeviceSettings::kEnableWaterSensor) { g_water_sensor.begin(); }

#if defined(PIPSURVIVOR_RADIO_ESPNOW)
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