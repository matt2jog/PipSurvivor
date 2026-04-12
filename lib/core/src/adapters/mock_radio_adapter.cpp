#include "mock_radio_adapter.h"

MockRadioAdapter::MockRadioAdapter(const RadioConfig& config)
    : RadioPort(config), outbox_count_(0), incoming_count_(0) {}

bool MockRadioAdapter::sendMessage(const String& message, uint8_t hops) {
  if (outbox_count_ >= kMaxPackets) {
    return false;
  }

  outbox_[outbox_count_] = OutgoingPacket{message, hops, false};
  outbox_count_++;
  return true;
}

bool MockRadioAdapter::sendAlert(const String& alert, uint8_t hops) {
  if (outbox_count_ >= kMaxPackets) {
    return false;
  }

  outbox_[outbox_count_] = OutgoingPacket{alert, hops, true};
  outbox_count_++;
  return true;
}

bool MockRadioAdapter::queueIncoming(const String& message) {
  if (incoming_count_ >= kMaxPackets) {
    return false;
  }

  incoming_[incoming_count_] = message;
  incoming_count_++;
  return true;
}

void MockRadioAdapter::poll() {
  if (incoming_count_ == 0) {
    return;
  }

  const String next_message = incoming_[0];
  for (size_t i = 1; i < incoming_count_; ++i) {
    incoming_[i - 1] = incoming_[i];
  }
  incoming_count_--;

  notifyMessageReceived(next_message, 0);
}

size_t MockRadioAdapter::outboxCount() const {
  return outbox_count_;
}

const MockRadioAdapter::OutgoingPacket& MockRadioAdapter::outboxAt(size_t index) const {
  static const OutgoingPacket kEmpty = OutgoingPacket{"", 0, false};
  if (index >= outbox_count_) {
    return kEmpty;
  }
  return outbox_[index];
}