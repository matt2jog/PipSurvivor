#include "espnow_radio_adapter.h"

EspNowRadioAdapter* EspNowRadioAdapter::active_instance_ = nullptr;

namespace {

String buildWirePayload(char type, uint8_t hops, const String& message) {
  return String(type) + "|" + String(hops) + "|" + message;
}

}  // namespace

EspNowRadioAdapter::EspNowRadioAdapter(const RadioConfig& config, uint8_t channel)
    : RadioPort(config),
      started_(false),
      channel_(channel),
      broadcast_peer_{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
      incoming_count_(0) {}

bool EspNowRadioAdapter::begin() {
#if !defined(ESP32)
  return false;
#else
  if (started_) {
    return true;
  }

  if (WiFi.getMode() == WIFI_OFF) {
    WiFi.mode(WIFI_STA);
  }

  if (esp_now_init() != ESP_OK) {
    return false;
  }

  active_instance_ = this;
  esp_now_register_recv_cb(EspNowRadioAdapter::onReceiveStatic);

  esp_now_peer_info_t peer_info{};
  memcpy(peer_info.peer_addr, broadcast_peer_, sizeof(broadcast_peer_));
  peer_info.channel = channel_;
  peer_info.encrypt = false;

  if (!esp_now_is_peer_exist(broadcast_peer_)) {
    if (esp_now_add_peer(&peer_info) != ESP_OK) {
      return false;
    }
  }

  started_ = true;
  return true;
#endif
}

bool EspNowRadioAdapter::sendMessage(const String& message, uint8_t hops) {
  return sendWirePayload('M', message, hops);
}

bool EspNowRadioAdapter::sendAlert(const String& alert, uint8_t hops) {
  return sendWirePayload('A', alert, hops);
}

bool EspNowRadioAdapter::sendWirePayload(char type, const String& payload, uint8_t hops) {
#if !defined(ESP32)
  (void)type;
  (void)payload;
  (void)hops;
  return false;
#else
  if (!started_ && !begin()) {
    return false;
  }

  const String wire_payload = buildWirePayload(type, hops, payload);
  const esp_err_t send_result =
      esp_now_send(broadcast_peer_, reinterpret_cast<const uint8_t*>(wire_payload.c_str()), wire_payload.length());
  return send_result == ESP_OK;
#endif
}

void EspNowRadioAdapter::poll() {
  if (incoming_count_ == 0) {
    return;
  }

  const String next_message = incoming_[0];
  for (size_t i = 1; i < incoming_count_; ++i) {
    incoming_[i - 1] = incoming_[i];
  }
  incoming_count_--;

  uint8_t hops = 0;
  const int firstPipe = next_message.indexOf('|');
  if (firstPipe > 0) {
    const int secondPipe = next_message.indexOf('|', firstPipe + 1);
    if (secondPipe > firstPipe) {
      hops = static_cast<uint8_t>(next_message.substring(firstPipe + 1, secondPipe).toInt());
    }
  }

  notifyMessageReceived(next_message, hops);
}

bool EspNowRadioAdapter::enqueueIncoming(const String& rawPayload) {
  if (incoming_count_ >= kMaxIncomingQueue) {
    return false;
  }

  incoming_[incoming_count_] = unwrapWirePayload(rawPayload);
  incoming_count_++;
  return true;
}

String EspNowRadioAdapter::unwrapWirePayload(const String& wirePayload) const {
  const int firstPipe = wirePayload.indexOf('|');
  if (firstPipe < 0) {
    return wirePayload;
  }

  const int secondPipe = wirePayload.indexOf('|', firstPipe + 1);
  if (secondPipe < 0) {
    return wirePayload;
  }

  return wirePayload.substring(secondPipe + 1);
}

#if defined(ESP32)
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void EspNowRadioAdapter::onReceiveStatic(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  (void)info;
  if (active_instance_ == nullptr) {
    return;
  }
  active_instance_->onReceive(data, len);
}
#else
void EspNowRadioAdapter::onReceiveStatic(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
  if (active_instance_ == nullptr) {
    return;
  }
  active_instance_->onReceive(data, len);
}
#endif

void EspNowRadioAdapter::onReceive(const uint8_t* data, int len) {
  if (data == nullptr || len <= 0) {
    return;
  }

  String payload;
  payload.reserve(static_cast<size_t>(len));
  for (int i = 0; i < len; ++i) {
    payload += static_cast<char>(data[i]);
  }

  enqueueIncoming(payload);
}
#endif