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
  bool sendMessage(const String& message, uint8_t hops) override;
  bool sendAlert(const String& alert, uint8_t hops) override;
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
    uint32_t hash;
    uint32_t timestamp_ms;
  };

  bool enqueueIncoming(const String& message, uint8_t hops);
  bool isDuplicate(const String& message) const;
  uint32_t hashMessage(const String& message) const;

  static void onSubAdapterMessageStatic(const String& message, uint8_t hops);
  static DualRadioAdapter* active_instance_;

  RadioPort& primary_;
  RadioPort& secondary_;

  String incoming_[kMaxIncomingQueue];
  uint8_t incoming_hops_[kMaxIncomingQueue];
  size_t incoming_count_;

  mutable DedupEntry dedup_cache_[kDedupCacheSize];
  mutable size_t dedup_index_;

  bool espnow_tx_enabled_;
  bool rylr998_tx_enabled_;
};

#endif
