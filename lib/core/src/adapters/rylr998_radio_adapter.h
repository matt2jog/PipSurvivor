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

  bool begin() override;

  bool sendMessage(const String& message, uint8_t hops) override;
  bool sendAlert(const String& alert, uint8_t hops) override;
  void poll() override;

  uint32_t getDeviceUid() const;

 private:
  struct MessageCacheItem {
    uint32_t sender_uid;
    uint32_t msg_id;
  };

  struct RelayJob {
    bool active;
    uint32_t fire_time_ms;
    String type;
    uint32_t sender_uid;
    uint32_t dest_uid;
    uint32_t msg_id;
    uint8_t ttl;
    String payload;
  };
  bool sendPayload(const String& payload);
  void handleLine(const String& line);
  String unwrapWirePayload(const String& wirePayload) const;

  HardwareSerial& serial_;
  uint16_t destination_address_;
  char line_buffer_[kLineBufferSize];
  size_t line_length_;
  bool initialized_;
  
  uint32_t my_uid_;
  uint32_t msg_seq_;

  MessageCacheItem cache_[50]; // Hardcoding to 50 for simplicity or use kMeshCacheSize
  size_t cache_index_;

  RelayJob relay_jobs_[5]; // Simple queue for pending relays
  
  void setupUid();
  bool isMessageSeen(uint32_t sender_uid, uint32_t msg_id);
  void markMessageSeen(uint32_t sender_uid, uint32_t msg_id);
  
  void scheduleRelay(const String& type, uint32_t sender_uid, uint32_t dest_uid, uint32_t msg_id, uint8_t ttl, const String& payload);
  void processRelayJobs();
  void cancelRelayIfMatches(uint32_t sender_uid, uint32_t msg_id);
  
  void sendDirectedAck(uint32_t dest_uid, uint32_t msg_id);
  bool sendRawMeshMessage(const String& type, uint32_t sender_uid, uint32_t dest_uid, uint32_t msg_id, uint8_t ttl, const String& payload);
};

#endif