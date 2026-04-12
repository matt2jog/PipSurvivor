#include "dual_radio_adapter.h"

DualRadioAdapter* DualRadioAdapter::active_instance_ = nullptr;

DualRadioAdapter::DualRadioAdapter(RadioPort& primary, RadioPort& secondary)
    : RadioPort(RadioConfig{"dual-radio", "dual"}),
      primary_(primary),
      secondary_(secondary),
      incoming_count_(0),
      dedup_index_(0),
      relay_count_(0),
      espnow_tx_enabled_(true),
      rylr998_tx_enabled_(true) {
  memset(dedup_cache_, 0, sizeof(dedup_cache_));
  for (size_t i = 0; i < 5; ++i) relay_jobs_[i].active = false;
}

bool DualRadioAdapter::begin() {
  active_instance_ = this;

  primary_.setReceiveCallback(onPrimaryMessageStatic);
  secondary_.setReceiveCallback(onSecondaryMessageStatic);

  bool primary_ok = primary_.begin();
  bool secondary_ok = secondary_.begin();

  Serial.printf("[DUAL_RADIO] begin: primary=%d secondary=%d\n", primary_ok, secondary_ok);
  return primary_ok || secondary_ok;
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
  processRelayJobs();

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
  }
}

uint32_t DualRadioAdapter::getDeviceUid() const {
  return primary_.getDeviceUid();
}

void DualRadioAdapter::getRecentMessages(MeshMessageEntry* out, size_t max, size_t& count) {
  size_t primary_count = 0;
  primary_.getRecentMessages(out, max, primary_count);
  count = primary_count;

  if (count < max) {
    size_t secondary_count = 0;
    secondary_.getRecentMessages(out + count, max - count, secondary_count);
    count += secondary_count;
  }
}

void DualRadioAdapter::getMeshStats(uint32_t& total_rx, size_t& cache_size, size_t& relay_jobs) const {
  uint32_t p_rx = 0, s_rx = 0;
  size_t p_cache = 0, s_cache = 0, p_relay = 0, s_relay = 0;
  primary_.getMeshStats(p_rx, p_cache, p_relay);
  secondary_.getMeshStats(s_rx, s_cache, s_relay);
  total_rx = p_rx + s_rx;
  cache_size = p_cache + s_cache;
  relay_jobs = p_relay + s_relay;
}

void DualRadioAdapter::onPrimaryMessageStatic(const String& message, uint8_t hops, uint32_t msg_id) {
  if (active_instance_ != nullptr) {
    active_instance_->enqueueIncomingWithSource(&active_instance_->primary_, message, hops, msg_id);
  }
}

void DualRadioAdapter::onSecondaryMessageStatic(const String& message, uint8_t hops, uint32_t msg_id) {
  if (active_instance_ != nullptr) {
    active_instance_->enqueueIncomingWithSource(&active_instance_->secondary_, message, hops, msg_id);
  }
}

bool DualRadioAdapter::enqueueIncomingWithSource(RadioPort* source, const String& message, uint8_t hops, uint32_t msg_id) {
  if (isDuplicate(msg_id)) {
    Serial.printf("[DUAL_RADIO] dedup drop: msg=%08X\n", msg_id);
    return false;
  }

  uint32_t now = millis();
  dedup_cache_[dedup_index_] = DedupEntry{msg_id, now};
  dedup_index_ = (dedup_index_ + 1) % kDedupCacheSize;

  // Cross-radio relay: if RX on A, relay on B
  if (hops > 0) {
    scheduleCrossRelay(source, msg_id, hops - 1, message);
  }

  if (incoming_count_ >= kMaxIncomingQueue) {
    Serial.println("[DUAL_RADIO] incoming queue full, dropping");
    return false;
  }

  incoming_[incoming_count_] = message;
  incoming_hops_[incoming_count_] = hops;
  incoming_msg_ids_[incoming_count_] = msg_id;
  incoming_count_++;
  return true;
}

void DualRadioAdapter::scheduleCrossRelay(RadioPort* source, uint32_t msg_id, uint8_t ttl, const String& payload) {
  for (size_t i = 0; i < 5; ++i) {
    if (!relay_jobs_[i].active) {
      relay_jobs_[i].active = true;
      uint32_t jitter = random(100, 501);
      relay_jobs_[i].fire_time_ms = millis() + jitter;
      relay_jobs_[i].source_radio = source;
      relay_jobs_[i].msg_id = msg_id;
      relay_jobs_[i].ttl = ttl;
      relay_jobs_[i].payload = payload;
      Serial.printf("[DUAL_RADIO] CROSS-RELAY scheduled msg=%08X ttl=%d delay=%lums\n", 
          msg_id, ttl, (unsigned long)jitter);
      break;
    }
  }
}

void DualRadioAdapter::processRelayJobs() {
  uint32_t now = millis();
  for (size_t i = 0; i < 5; ++i) {
    if (relay_jobs_[i].active && now >= relay_jobs_[i].fire_time_ms) {
      relay_jobs_[i].active = false;
      
      // If source was primary (RYLR), relay on secondary (ESPNOW)
      // If source was secondary (ESPNOW), relay on primary (RYLR)
      if (relay_jobs_[i].source_radio == &primary_) {
        if (espnow_tx_enabled_) {
          Serial.printf("[DUAL_RADIO] RELAY RYLR->ESPNOW msg=%08X\n", relay_jobs_[i].msg_id);
          secondary_.sendMessage(relay_jobs_[i].payload, relay_jobs_[i].ttl, relay_jobs_[i].msg_id);
        }
      } else {
        if (rylr998_tx_enabled_) {
          Serial.printf("[DUAL_RADIO] RELAY ESPNOW->RYLR msg=%08X\n", relay_jobs_[i].msg_id);
          primary_.sendMessage(relay_jobs_[i].payload, relay_jobs_[i].ttl, relay_jobs_[i].msg_id);
        }
      }
    }
  }
}

bool DualRadioAdapter::sendMessage(const String& message, uint8_t hops, uint32_t msg_id) {
  bool a = rylr998_tx_enabled_ ? primary_.sendMessage(message, hops, msg_id) : false;
  bool b = espnow_tx_enabled_ ? secondary_.sendMessage(message, hops, msg_id) : false;
  Serial.printf("[DUAL_RADIO] sendMessage: rylr998=%d espnow=%d\n", a, b);
  return a || b;
}

bool DualRadioAdapter::sendAlert(const String& alert, uint8_t hops, uint32_t msg_id) {
  bool a = rylr998_tx_enabled_ ? primary_.sendAlert(alert, hops, msg_id) : false;
  bool b = espnow_tx_enabled_ ? secondary_.sendAlert(alert, hops, msg_id) : false;
  Serial.printf("[DUAL_RADIO] sendAlert: rylr998=%d espnow=%d\n", a, b);
  return a || b;
}

bool DualRadioAdapter::enqueueIncoming(const String& message, uint8_t hops, uint32_t msg_id) {
  return enqueueIncomingWithSource(nullptr, message, hops, msg_id);
}

bool DualRadioAdapter::isDuplicate(uint32_t msg_id) const {
  if (msg_id == 0) return false;
  uint32_t now = millis();
  for (size_t i = 0; i < kDedupCacheSize; ++i) {
    if (dedup_cache_[i].msg_id == msg_id && (now - dedup_cache_[i].timestamp_ms) < 5000) {
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