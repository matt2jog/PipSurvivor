#ifndef PIPSURVIVOR_DUAL_RADIO_ADAPTER_H
#define PIPSURVIVOR_DUAL_RADIO_ADAPTER_H

#include <Arduino.h>

#include "../ports/radio_port.h"

class DualRadioAdapter : public RadioPort {
 public:
  static const size_t kMaxIncomingQueue = 16;
  static const size_t kDedupCacheSize = 32;

  DualRadioAdapter(RadioPort& primary, RadioPort& secondary);

  bool begin() override;
  bool sendMessage(const String& message, uint8_t hops, uint32_t msg_id = 0) override;
  bool sendAlert(const String& alert, uint8_t hops, uint32_t msg_id = 0) override;
  void poll() override;

  uint32_t getDeviceUid() const override;
  void getRecentMessages(MeshMessageEntry* out, size_t max, size_t& count) override;
  void getMeshStats(uint32_t& total_rx, size_t& cache_size, size_t& relay_jobs) const override;

  size_t primaryOutboxCount() const;
  size_t secondaryOutboxCount() const;

  void enableEspNow(bool enable);
  void enableRylr998(bool enable);
  bool isEspNowTxEnabled() const;
  bool isRylr998TxEnabled() const;

 private:
  struct DedupEntry {
    uint32_t msg_id;
    uint32_t timestamp_ms;
  };

  struct RelayJob {
    bool active;
    uint32_t fire_time_ms;
    RadioPort* source_radio; // The radio we received it on
    uint32_t msg_id;
    uint8_t ttl;
    String payload;
  };

  void scheduleCrossRelay(RadioPort* source, uint32_t msg_id, uint8_t ttl, const String& payload);
  void processRelayJobs();

  bool enqueueIncoming(const String& message, uint8_t hops, uint32_t msg_id);
  bool enqueueIncomingWithSource(RadioPort* source, const String& message, uint8_t hops, uint32_t msg_id);
  bool isDuplicate(uint32_t msg_id) const;
  uint32_t hashMessage(const String& message) const;

  static void onPrimaryMessageStatic(const String& message, uint8_t hops, uint32_t msg_id);
  static void onSecondaryMessageStatic(const String& message, uint8_t hops, uint32_t msg_id);
  static void onPrimaryAckStatic(uint32_t msg_id);
  static void onSecondaryAckStatic(uint32_t msg_id);
  static void onPrimaryWaitStatic();
  static void onSecondaryWaitStatic();
  void onAckFrom(RadioPort* source, uint32_t msg_id);
  static DualRadioAdapter* active_instance_;

  RadioPort& primary_;
  RadioPort& secondary_;

  String incoming_[kMaxIncomingQueue];
  uint8_t incoming_hops_[kMaxIncomingQueue];
  uint32_t incoming_msg_ids_[kMaxIncomingQueue];
  size_t incoming_count_;

  mutable DedupEntry dedup_cache_[kDedupCacheSize];
  mutable size_t dedup_index_;

  RelayJob relay_jobs_[5];
  size_t relay_count_;

  bool espnow_tx_enabled_;
  bool rylr998_tx_enabled_;
};

#endif
