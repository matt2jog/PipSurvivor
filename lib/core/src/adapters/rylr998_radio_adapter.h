#ifndef PIPSURVIVOR_RYLR998_RADIO_ADAPTER_H
#define PIPSURVIVOR_RYLR998_RADIO_ADAPTER_H

#include <Arduino.h>

#include "../ports/radio_port.h"
#include "../settings.h"

class Rylr998RadioAdapter : public RadioPort {
 public:
  static const size_t kLineBufferSize = 192;

  Rylr998RadioAdapter(
      const RadioConfig& config,
      HardwareSerial& serial,
      uint16_t destinationAddress,
      uint32_t baudRate = 115200);

  bool begin() override;

  bool sendMessage(const String& message, uint8_t hops, uint32_t msg_id = 0) override;
  bool sendAlert(const String& alert, uint8_t hops, uint32_t msg_id = 0) override;
  void poll() override;

  uint32_t getDeviceUid() const override;
  void getRecentMessages(MeshMessageEntry* out, size_t max, size_t& count) override;
  void getMeshStats(uint32_t& total_rx, size_t& cache_size, size_t& relay_jobs) const override;

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
  volatile uint32_t acked_msg_id_;

  MessageCacheItem cache_[50]; // Hardcoding to 50 for simplicity or use kMeshCacheSize
  size_t cache_index_;

  RelayJob relay_jobs_[5]; // Simple queue for pending relays

  MeshMessageEntry message_history_[DeviceSettings::kMeshHistorySize];
  size_t history_count_;
  size_t history_head_;
  uint32_t total_rx_;
  
  void setupUid();
  bool isMessageSeen(uint32_t sender_uid, uint32_t msg_id);
  void markMessageSeen(uint32_t sender_uid, uint32_t msg_id);
  
  void scheduleRelay(const String& type, uint32_t sender_uid, uint32_t dest_uid, uint32_t msg_id, uint8_t ttl, const String& payload);
  void processRelayJobs();
  void cancelRelayIfMatches(uint32_t sender_uid, uint32_t msg_id);
  
  void sendDirectedAck(uint32_t dest_uid, uint32_t msg_id);
  bool sendWithAck(const String& type, uint32_t msg_id, uint8_t hops, const String& payload);
  bool sendRawMeshMessage(const String& type, uint32_t sender_uid, uint32_t dest_uid, uint32_t msg_id, uint8_t ttl, const String& payload);
};

#endif