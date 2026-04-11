#ifndef PIPSURVIVOR_ESPNOW_RADIO_ADAPTER_H
#define PIPSURVIVOR_ESPNOW_RADIO_ADAPTER_H

#include <Arduino.h>

#include "../ports/radio_port.h"

#if defined(ESP32)
#include <WiFi.h>
#include <esp_now.h>
#endif

class EspNowRadioAdapter : public RadioPort {
 public:
  static const size_t kMaxIncomingQueue = 16;

  explicit EspNowRadioAdapter(const RadioConfig& config, uint8_t channel = 0);

  bool begin();
  bool sendMessage(const String& message, uint8_t hops) override;
  bool sendAlert(const String& alert, uint8_t hops) override;
  void poll() override;

 private:
  bool enqueueIncoming(const String& rawPayload);
  bool sendWirePayload(char type, const String& payload, uint8_t hops);
  String unwrapWirePayload(const String& wirePayload) const;

#if defined(ESP32)
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  static void onReceiveStatic(const esp_now_recv_info_t* info, const uint8_t* data, int len);
#else
  static void onReceiveStatic(const uint8_t* mac, const uint8_t* data, int len);
#endif
  void onReceive(const uint8_t* data, int len);
#endif

  bool started_;
  uint8_t channel_;
  uint8_t broadcast_peer_[6];

  String incoming_[kMaxIncomingQueue];
  size_t incoming_count_;

  static EspNowRadioAdapter* active_instance_;
};

#endif