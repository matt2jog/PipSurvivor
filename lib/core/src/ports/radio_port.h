#ifndef PIPSURVIVOR_RADIO_PORT_H
#define PIPSURVIVOR_RADIO_PORT_H

#include <Arduino.h>

struct RadioConfig {
  String deviceType;
  String identifier;
};

typedef void (*RadioReceiveCallback)(const String& message);

class RadioPort {
 public:
  explicit RadioPort(const RadioConfig& config);
  virtual ~RadioPort() = default;

  virtual bool sendMessage(const String& message, uint8_t hops) = 0;
  virtual bool sendAlert(const String& alert, uint8_t hops) = 0;

  // Called from loop() so the adapter can process incoming packets.
  virtual void poll() = 0;

  void setReceiveCallback(RadioReceiveCallback callback);

 protected:
  void notifyMessageReceived(const String& message) const;

  RadioConfig config_;

 private:
  RadioReceiveCallback receive_callback_;
};

#endif