#include "radio_port.h"

RadioPort::RadioPort(const RadioConfig& config)
  : config_(config), receive_callback_(nullptr), ack_callback_(nullptr) {}

void RadioPort::setReceiveCallback(RadioReceiveCallback callback) {
  receive_callback_ = callback;
}

void RadioPort::setAckCallback(RadioAckCallback callback) {
  ack_callback_ = callback;
}

void RadioPort::notifyMessageReceived(const String& message, uint8_t hops, uint32_t msg_id) const {
  if (receive_callback_ == nullptr) {
    return;
  }
  receive_callback_(message, hops, msg_id);
}

void RadioPort::notifyAckReceived(uint32_t msg_id) const {
  if (ack_callback_ == nullptr) {
    return;
  }
  ack_callback_(msg_id);
}