#include "rylr998_radio_adapter.h"
#include "../settings.h"

namespace {

String buildWirePayload(char type, uint8_t hops, const String& message) {
  return String(type) + "|" + String(hops) + "|" + message;
}

}  // namespace

Rylr998RadioAdapter::Rylr998RadioAdapter(
    const RadioConfig& config,
    HardwareSerial& serial,
    uint16_t destinationAddress,
    uint32_t baudRate)
    : RadioPort(config),
      serial_(serial),
      destination_address_(destinationAddress),
      line_length_(0),
      initialized_(false) {
  serial_.begin(baudRate);
  memset(line_buffer_, 0, sizeof(line_buffer_));
}

bool Rylr998RadioAdapter::begin() {
  if (initialized_) {
    return true;
  }

  serial_.println("AT+BAND=" + String(DeviceSettings::kRadioBand));
  delay(50);
  serial_.println("AT+NETWORKID=" + String(DeviceSettings::kRadioNetworkId));
  delay(50);
  serial_.println("AT+PARAMETER=" + String(DeviceSettings::kRadioSf) + "," +
                  String(DeviceSettings::kRadioCr) + "," +
                  String(DeviceSettings::kRadioBw) + "," +
                  String(DeviceSettings::kRadioPreamble));
  delay(50);
  serial_.println("AT+ADDRESS=" + String(DeviceSettings::SENDER
      ? DeviceSettings::kRadioAddressSender
      : DeviceSettings::kRadioAddressReceiver));
  delay(500);

  initialized_ = true;
  return true;
}

bool Rylr998RadioAdapter::sendMessage(const String& message, uint8_t hops) {
  return sendPayload(buildWirePayload('M', hops, message));
}

bool Rylr998RadioAdapter::sendAlert(const String& alert, uint8_t hops) {
  return sendPayload(buildWirePayload('A', hops, alert));
}

bool Rylr998RadioAdapter::sendPayload(const String& payload) {
  if (payload.length() == 0) {
    return false;
  }

  const String cmd =
      "AT+SEND=" + String(destination_address_) + "," + String(payload.length()) + "," + payload;
  serial_.println(cmd);
  return true;
}

void Rylr998RadioAdapter::poll() {
  while (serial_.available() > 0) {
    const char ch = static_cast<char>(serial_.read());

    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      if (line_length_ > 0) {
        line_buffer_[line_length_] = '\0';
        handleLine(String(line_buffer_));
        line_length_ = 0;
      }
      continue;
    }

    if (line_length_ < kLineBufferSize - 1) {
      line_buffer_[line_length_] = ch;
      line_length_++;
    }
  }
}

void Rylr998RadioAdapter::handleLine(const String& line) {
  if (!line.startsWith("+RCV=")) {
    return;
  }

  const int firstComma = line.indexOf(',');
  if (firstComma < 0) {
    return;
  }

  const int secondComma = line.indexOf(',', firstComma + 1);
  if (secondComma < 0) {
    return;
  }

  const int thirdComma = line.indexOf(',', secondComma + 1);
  if (thirdComma < 0) {
    return;
  }

  const String wirePayload = line.substring(secondComma + 1, thirdComma);
  const String message = unwrapWirePayload(wirePayload);
  if (message.length() > 0) {
    notifyMessageReceived(message);
  }
}

String Rylr998RadioAdapter::unwrapWirePayload(const String& wirePayload) const {
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