#ifndef PIPSURVIVOR_RYLR998_RADIO_ADAPTER_H
#define PIPSURVIVOR_RYLR998_RADIO_ADAPTER_H

#include <Arduino.h>

#include "../ports/radio_port.h"

class Rylr998RadioAdapter : public RadioPort {
 public:
  static const size_t kLineBufferSize = 192;

  Rylr998RadioAdapter(
      const RadioConfig& config,
      HardwareSerial& serial,
      uint16_t destinationAddress,
      uint32_t baudRate = 115200);

  bool sendMessage(const String& message, uint8_t hops) override;
  bool sendAlert(const String& alert, uint8_t hops) override;
  void poll() override;

 private:
  bool sendPayload(const String& payload);
  void handleLine(const String& line);
  String unwrapWirePayload(const String& wirePayload) const;

  HardwareSerial& serial_;
  uint16_t destination_address_;
  char line_buffer_[kLineBufferSize];
  size_t line_length_;
};

#endif