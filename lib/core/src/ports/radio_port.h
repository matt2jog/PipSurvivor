#ifndef PIPSURVIVOR_RADIO_PORT_H
#define PIPSURVIVOR_RADIO_PORT_H

#include <Arduino.h>

struct RadioConfig {
  String deviceType;
  String identifier;
};

typedef void (*RadioReceiveCallback)(const String& message, uint8_t hops);

struct MeshMessageEntry {
  uint32_t sender_uid;
  uint32_t msg_id;
  uint8_t ttl;
  String payload;
  uint32_t recv_time_ms;
};

class RadioPort {
 public:
  explicit RadioPort(const RadioConfig& config);
  virtual ~RadioPort() = default;

  virtual bool sendMessage(const String& message, uint8_t hops) = 0;
  virtual bool sendAlert(const String& alert, uint8_t hops) = 0;

  // Called once during setup to configure the hardware/module.
  virtual bool begin() { return true; }

  // Called from loop() so the adapter can process incoming packets.
  virtual void poll() = 0;

  void setReceiveCallback(RadioReceiveCallback callback);

  virtual uint32_t getDeviceUid() const { return 0; }
  virtual void getRecentMessages(MeshMessageEntry* out, size_t max, size_t& count) { count = 0; }
  virtual void getMeshStats(uint32_t& total_rx, size_t& cache_size, size_t& relay_jobs) const {
    total_rx = 0; cache_size = 0; relay_jobs = 0;
  }

 protected:
  void notifyMessageReceived(const String& message, uint8_t hops) const;

  RadioConfig config_;

 private:
  RadioReceiveCallback receive_callback_;
};

#endif