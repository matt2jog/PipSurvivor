#ifndef PIPSURVIVOR_MOCK_RADIO_ADAPTER_H
#define PIPSURVIVOR_MOCK_RADIO_ADAPTER_H

#include <Arduino.h>

#include "../ports/radio_port.h"

class MockRadioAdapter : public RadioPort {
 public:
  static const size_t kMaxPackets = 16;

  struct OutgoingPacket {
    String payload;
    uint8_t hops;
    bool isAlert;
  };

  explicit MockRadioAdapter(const RadioConfig& config);

  bool sendMessage(const String& message, uint8_t hops) override;
  bool sendAlert(const String& alert, uint8_t hops) override;
  void poll() override;

  bool queueIncoming(const String& message);

  size_t outboxCount() const;
  const OutgoingPacket& outboxAt(size_t index) const;

 private:
  OutgoingPacket outbox_[kMaxPackets];
  size_t outbox_count_;

  String incoming_[kMaxPackets];
  size_t incoming_count_;
};

#endif