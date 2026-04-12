#include "espnow_radio_adapter.h"

#include "../settings.h"

EspNowRadioAdapter* EspNowRadioAdapter::active_instance_ = nullptr;

namespace {

String buildWirePayload(char type, uint32_t msg_id, uint8_t hops, const String& message) {
  return String(type) + "|" + String(msg_id) + "|" + String(hops) + "|" + message;
}

}  // namespace

EspNowRadioAdapter::EspNowRadioAdapter(const RadioConfig& config, uint8_t channel)
    : RadioPort(config),
      started_(false),
      channel_(channel),
      broadcast_peer_{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
      incoming_count_(0),
      acked_msg_id_(0) {}

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

bool EspNowRadioAdapter::sendMessage(const String& message, uint8_t hops, uint32_t msg_id) {
  // Use provided msg_id or generate a random one
  if (msg_id == 0) msg_id = esp_random();
  return sendWirePayload('M', message, hops, msg_id, true);
}

bool EspNowRadioAdapter::sendAlert(const String& alert, uint8_t hops, uint32_t msg_id) {
  if (msg_id == 0) msg_id = esp_random();
  return sendWirePayload('A', alert, hops, msg_id, true);
}

bool EspNowRadioAdapter::sendWirePayload(char type, const String& payload, uint8_t hops, uint32_t msg_id, bool needs_ack) {
#if !defined(ESP32)
  (void)type;
  (void)payload;
  (void)hops;
  (void)msg_id;
  (void)needs_ack;
  return false;
#else
  if (!started_ && !begin()) {
    return false;
  }

  const String wire_payload = buildWirePayload(type, msg_id, hops, payload);
  int max_retries = needs_ack ? DeviceSettings::kEspNowMaxRetries : 1;
  uint32_t retry_delay_ms = DeviceSettings::kEspNowRetryDelayMs;

  for (int attempt = 0; attempt < max_retries; ++attempt) {
    esp_err_t send_result = esp_now_send(
        broadcast_peer_, reinterpret_cast<const uint8_t*>(wire_payload.c_str()), wire_payload.length());

    if (send_result != ESP_OK) {
      delay(retry_delay_ms);
      continue;
    }

    if (!needs_ack) {
      return true;
    }

    // Wait for ACK
    uint32_t start_time = millis();
    while (millis() - start_time < retry_delay_ms) {
      if (acked_msg_id_ == msg_id) {
        return true;  // ACK received successfully
      }
      delay(10);
    }
  }
  return false;
#endif
}

void EspNowRadioAdapter::poll() {
  if (incoming_count_ == 0) {
    return;
  }

  String raw = incoming_[0];
  uint8_t hops = incoming_hops_[0];
  for (size_t i = 1; i < incoming_count_; ++i) {
    incoming_[i - 1] = incoming_[i];
    incoming_hops_[i - 1] = incoming_hops_[i];
  }
  incoming_count_--;

  String msg = unwrapWirePayload(raw);
  uint32_t msg_id = parseMsgIdFromWirePayload(raw);
  notifyMessageReceived(msg, hops, msg_id);
}

bool EspNowRadioAdapter::enqueueIncoming(const String& rawPayload, uint8_t hops) {
  if (incoming_count_ >= kMaxIncomingQueue) {
    return false;
  }

  incoming_[incoming_count_] = rawPayload;
  incoming_hops_[incoming_count_] = hops;
  incoming_count_++;
  return true;
}

uint32_t EspNowRadioAdapter::parseMsgIdFromWirePayload(const String& wirePayload) const {
  const int firstPipe = wirePayload.indexOf('|');
  if (firstPipe < 0) {
    return 0;
  }
  const int secondPipe = wirePayload.indexOf('|', firstPipe + 1);
  if (secondPipe < 0) {
    return 0;
  }
  return static_cast<uint32_t>(wirePayload.substring(firstPipe + 1, secondPipe).toInt());
}

uint8_t EspNowRadioAdapter::parseHopsFromWirePayload(const String& wirePayload) const {
  const int firstPipe = wirePayload.indexOf('|');
  if (firstPipe < 0) return 0;
  const int secondPipe = wirePayload.indexOf('|', firstPipe + 1);
  if (secondPipe < 0) return 0;
  const int thirdPipe = wirePayload.indexOf('|', secondPipe + 1);
  if (thirdPipe < 0) return 0;
  return static_cast<uint8_t>(wirePayload.substring(secondPipe + 1, thirdPipe).toInt());
}

String EspNowRadioAdapter::unwrapWirePayload(const String& wirePayload) const {
  const int firstPipe = wirePayload.indexOf('|');
  if (firstPipe < 0) return wirePayload;
  const int secondPipe = wirePayload.indexOf('|', firstPipe + 1);
  if (secondPipe < 0) return wirePayload;
  const int thirdPipe = wirePayload.indexOf('|', secondPipe + 1);
  if (thirdPipe < 0) return wirePayload;

  return wirePayload.substring(thirdPipe + 1);
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

  String rawPayload;
  rawPayload.reserve(static_cast<size_t>(len));
  for (int i = 0; i < len; ++i) {
    rawPayload += static_cast<char>(data[i]);
  }

  char type = rawPayload[0];
  if (type == 'K') {
    // ACK message received
    uint32_t ack_id = parseMsgIdFromWirePayload(rawPayload);
    acked_msg_id_ = ack_id;
    return;
  }

  // Not an ACK message, so we must ACK it and enqueue for our application
  uint32_t msg_id = parseMsgIdFromWirePayload(rawPayload);
  if (msg_id != 0) {
    sendWirePayload('K', "", 0, msg_id, false);  // Send ACK back
  }

  uint8_t hops = parseHopsFromWirePayload(rawPayload);
  enqueueIncoming(rawPayload, hops);
}
#endif