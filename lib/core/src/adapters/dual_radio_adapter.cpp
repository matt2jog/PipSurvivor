#include "dual_radio_adapter.h"

DualRadioAdapter* DualRadioAdapter::active_instance_ = nullptr;

DualRadioAdapter::DualRadioAdapter(RadioPort& primary, RadioPort& secondary)
    : RadioPort(RadioConfig{"dual-radio", "dual"}),
      primary_(primary),
      secondary_(secondary),
      incoming_count_(0),
      dedup_index_(0),
      espnow_tx_enabled_(true),
      rylr998_tx_enabled_(true) {
  memset(dedup_cache_, 0, sizeof(dedup_cache_));
}

bool DualRadioAdapter::begin() {
  active_instance_ = this;

  primary_.setReceiveCallback(onSubAdapterMessageStatic);
  secondary_.setReceiveCallback(onSubAdapterMessageStatic);

  bool primary_ok = primary_.begin();
  bool secondary_ok = secondary_.begin();

  Serial.printf("[DUAL_RADIO] begin: primary=%d secondary=%d\n", primary_ok, secondary_ok);
  return primary_ok || secondary_ok;
}

bool DualRadioAdapter::sendMessage(const String& message, uint8_t hops) {
  bool a = rylr998_tx_enabled_ ? primary_.sendMessage(message, hops) : false;
  bool b = espnow_tx_enabled_ ? secondary_.sendMessage(message, hops) : false;
  Serial.printf("[DUAL_RADIO] sendMessage: rylr998=%d espnow=%d\n", a, b);
  return a || b;
}

bool DualRadioAdapter::sendAlert(const String& alert, uint8_t hops) {
  bool a = rylr998_tx_enabled_ ? primary_.sendAlert(alert, hops) : false;
  bool b = espnow_tx_enabled_ ? secondary_.sendAlert(alert, hops) : false;
  Serial.printf("[DUAL_RADIO] sendAlert: rylr998=%d espnow=%d\n", a, b);
  return a || b;
}

void DualRadioAdapter::enableEspNow(bool enable) {
  espnow_tx_enabled_ = enable;
  Serial.printf("[DUAL_RADIO] espnow tx: %s\n", enable ? "ON" : "OFF");
}

void DualRadioAdapter::enableRylr998(bool enable) {
  rylr998_tx_enabled_ = enable;
  Serial.printf("[DUAL_RADIO] rylr998 tx: %s\n", enable ? "ON" : "OFF");
}

bool DualRadioAdapter::isEspNowTxEnabled() const {
  return espnow_tx_enabled_;
}

bool DualRadioAdapter::isRylr998TxEnabled() const {
  return rylr998_tx_enabled_;
}

void DualRadioAdapter::poll() {
  primary_.poll();
  secondary_.poll();

  while (incoming_count_ > 0) {
    String msg = incoming_[0];
    uint8_t hops = incoming_hops_[0];
    for (size_t i = 1; i < incoming_count_; ++i) {
      incoming_[i - 1] = incoming_[i];
      incoming_hops_[i - 1] = incoming_hops_[i];
    }
    incoming_count_--;

    notifyMessageReceived(msg, hops);
  }
}

uint32_t DualRadioAdapter::getDeviceUid() const {
  return primary_.getDeviceUid();
}

void DualRadioAdapter::getRecentMessages(MeshMessageEntry* out, size_t max, size_t& count) {
  primary_.getRecentMessages(out, max, count);
}

void DualRadioAdapter::getMeshStats(uint32_t& total_rx, size_t& cache_size, size_t& relay_jobs) const {
  primary_.getMeshStats(total_rx, cache_size, relay_jobs);
}

void DualRadioAdapter::onSubAdapterMessageStatic(const String& message, uint8_t hops) {
  if (active_instance_ != nullptr) {
    active_instance_->enqueueIncoming(message, hops);
  }
}

bool DualRadioAdapter::enqueueIncoming(const String& message, uint8_t hops) {
  if (isDuplicate(message)) {
    Serial.printf("[DUAL_RADIO] dedup drop: \"%s\"\n", message.c_str());
    return false;
  }

  if (incoming_count_ >= kMaxIncomingQueue) {
    Serial.println("[DUAL_RADIO] incoming queue full, dropping");
    return false;
  }

  uint32_t h = hashMessage(message);
  dedup_cache_[dedup_index_] = DedupEntry{h, millis()};
  dedup_index_ = (dedup_index_ + 1) % kDedupCacheSize;

  incoming_[incoming_count_] = message;
  incoming_hops_[incoming_count_] = hops;
  incoming_count_++;
  return true;
}

bool DualRadioAdapter::isDuplicate(const String& message) const {
  uint32_t h = hashMessage(message);
  uint32_t now = millis();
  for (size_t i = 0; i < kDedupCacheSize; ++i) {
    if (dedup_cache_[i].hash == h && (now - dedup_cache_[i].timestamp_ms) < 5000) {
      return true;
    }
  }
  return false;
}

uint32_t DualRadioAdapter::hashMessage(const String& message) const {
  uint32_t h = 2166136261UL;
  for (unsigned int i = 0; i < message.length(); ++i) {
    h ^= static_cast<uint8_t>(message[i]);
    h *= 16777619UL;
  }
  return h;
}
