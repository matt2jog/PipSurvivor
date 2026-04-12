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

bool DualRadioAdapter::sendMessage(const String& message, uint8_t hops, uint32_t msg_id) {
  if (msg_id == 0) msg_id = (millis() << 16) | (random(0, 0xFFFF));
  bool a = rylr998_tx_enabled_ ? primary_.sendMessage(message, hops, msg_id) : false;
  bool b = espnow_tx_enabled_ ? secondary_.sendMessage(message, hops, msg_id) : false;
  Serial.printf("[DUAL_RADIO] sendMessage: msg_id=%lu rylr998=%d espnow=%d\n", (unsigned long)msg_id, a, b);
  return a || b;
}

bool DualRadioAdapter::sendAlert(const String& alert, uint8_t hops, uint32_t msg_id) {
  if (msg_id == 0) msg_id = (millis() << 16) | (random(0, 0xFFFF));
  bool a = rylr998_tx_enabled_ ? primary_.sendAlert(alert, hops, msg_id) : false;
  bool b = espnow_tx_enabled_ ? secondary_.sendAlert(alert, hops, msg_id) : false;
  Serial.printf("[DUAL_RADIO] sendAlert: msg_id=%lu rylr998=%d espnow=%d\n", (unsigned long)msg_id, a, b);
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
    uint32_t msg_id = incoming_msg_ids_[0];
    for (size_t i = 1; i < incoming_count_; ++i) {
      incoming_[i - 1] = incoming_[i];
      incoming_hops_[i - 1] = incoming_hops_[i];
      incoming_msg_ids_[i - 1] = incoming_msg_ids_[i];
    }
    incoming_count_--;

    notifyMessageReceived(msg, hops, msg_id);
    
    // Relay if there are still hops remaining!
    if (hops > 1 && msg_id != 0) {
      if (relay_job_count_ < kMaxRelayJobs) {
        relay_jobs_[relay_job_count_++] = {
          (uint32_t)(millis() + random(100, 1500)), // Jitter
          msg,
          (uint8_t)(hops - 1),
          msg_id
        };
      }
    }
  }

  uint32_t now = millis();
  for (size_t i = 0; i < relay_job_count_; ) {
    if (now >= relay_jobs_[i].transmit_time_ms) {
      sendMessage(relay_jobs_[i].message, relay_jobs_[i].hops_remaining, relay_jobs_[i].msg_id);
      for (size_t j = i + 1; j < relay_job_count_; ++j) {
        relay_jobs_[j - 1] = relay_jobs_[j];
      }
      relay_job_count_--;
    } else {
      i++;
    }
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

void DualRadioAdapter::onSubAdapterMessageStatic(const String& message, uint8_t hops, uint32_t msg_id) {
  if (active_instance_ != nullptr) {
    active_instance_->enqueueIncoming(message, hops, msg_id);
  }
}

bool DualRadioAdapter::enqueueIncoming(const String& message, uint8_t hops, uint32_t msg_id) {
  if (isDuplicate(message, msg_id)) {
    Serial.printf("[DUAL_RADIO] dedup drop: \"%s\" msg_id=%lu\n", message.c_str(), (unsigned long)msg_id);
    return false;
  }

  if (incoming_count_ >= kMaxIncomingQueue) {
    Serial.println("[DUAL_RADIO] incoming queue full, dropping");
    return false;
  }

  uint32_t h = hashMessage(message);
  dedup_cache_[dedup_index_] = DedupEntry{h, millis(), msg_id};
  dedup_index_ = (dedup_index_ + 1) % kDedupCacheSize;

  incoming_[incoming_count_] = message;
  incoming_hops_[incoming_count_] = hops;
  incoming_msg_ids_[incoming_count_] = msg_id;
  incoming_count_++;
  return true;
}

bool DualRadioAdapter::isDuplicate(const String& message, uint32_t msg_id) const {
  uint32_t h = hashMessage(message);
  uint32_t now = millis();
  for (size_t i = 0; i < kDedupCacheSize; ++i) {
    if ((dedup_cache_[i].hash == h || (msg_id != 0 && dedup_cache_[i].msg_id == msg_id))
        && (now - dedup_cache_[i].timestamp_ms) < 5000) {
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
