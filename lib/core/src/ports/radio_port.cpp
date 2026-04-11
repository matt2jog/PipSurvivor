#include "radio_port.h"

RadioPort::RadioPort(const RadioConfig& config)
    : config_(config), receive_callback_(nullptr) {}

void RadioPort::setReceiveCallback(RadioReceiveCallback callback) {
  receive_callback_ = callback;
}

void RadioPort::notifyMessageReceived(const String& message) const {
  if (receive_callback_ == nullptr) {
    return;
  }
  receive_callback_(message);
}