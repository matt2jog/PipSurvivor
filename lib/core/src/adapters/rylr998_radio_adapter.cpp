#include "rylr998_radio_adapter.h"
#include "../settings.h"
#include <WiFi.h>

namespace {

// Removed simple buildWirePayload in favor of full mesh headers

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
      initialized_(false),
      my_uid_(0),
      msg_seq_(0),
      acked_msg_id_(0),
      cache_index_(0),
      history_count_(0),
      history_head_(0),
      total_rx_(0) {
  memset(line_buffer_, 0, sizeof(line_buffer_));
  
  for (size_t i = 0; i < DeviceSettings::kMeshCacheSize; ++i) {
    cache_[i] = MessageCacheItem{0, 0};
  }
  for (size_t i = 0; i < 5; ++i) {
    relay_jobs_[i].active = false;
  }
}

void Rylr998RadioAdapter::setupUid() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  // Derive UID from last 4 bytes of MAC
  my_uid_ = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
}

uint32_t Rylr998RadioAdapter::getDeviceUid() const {
  return my_uid_;
}

bool Rylr998RadioAdapter::isMessageSeen(uint32_t sender_uid, uint32_t msg_id) {
  for (size_t i = 0; i < DeviceSettings::kMeshCacheSize; ++i) {
    if (cache_[i].sender_uid == sender_uid && cache_[i].msg_id == msg_id) {
      return true;
    }
  }
  return false;
}

void Rylr998RadioAdapter::markMessageSeen(uint32_t sender_uid, uint32_t msg_id) {
  cache_[cache_index_] = MessageCacheItem{sender_uid, msg_id};
  cache_index_ = (cache_index_ + 1) % DeviceSettings::kMeshCacheSize;
}

bool Rylr998RadioAdapter::begin() {
  if (initialized_) {
    return true;
  }
  
  setupUid();
  serial_.begin(115200);

  delay(500);
  while (serial_.available()) serial_.read();

  Serial.println("[RADIO] Init: resetting...");
  serial_.println("AT+RESET");
  delay(300);
  while (serial_.available()) {
    Serial.print("[RADIO] reset resp: ");
    Serial.println(serial_.readString());
  }

  Serial.println("[RADIO] Setting BAND...");
  serial_.println("AT+BAND=" + String(DeviceSettings::kRadioBand));
  delay(300);
  {
    String resp;
    while (serial_.available()) resp += (char)serial_.read();
    Serial.print("[RADIO] BAND resp: ");
    Serial.println(resp.length() ? resp : "(none)");
  }

  Serial.println("[RADIO] Setting NETWORKID...");
  serial_.println("AT+NETWORKID=" + String(DeviceSettings::kRadioNetworkId));
  delay(300);
  {
    String resp;
    while (serial_.available()) resp += (char)serial_.read();
    Serial.print("[RADIO] NETWORKID resp: ");
    Serial.println(resp.length() ? resp : "(none)");
  }

  Serial.println("[RADIO] Setting PARAMETER...");
  serial_.println("AT+PARAMETER=" + String(DeviceSettings::kRadioSf) + "," +
                  String(DeviceSettings::kRadioCr) + "," +
                  String(DeviceSettings::kRadioBw) + "," +
                  String(DeviceSettings::kRadioPreamble));
  delay(300);
  {
    String resp;
    while (serial_.available()) resp += (char)serial_.read();
    Serial.print("[RADIO] PARAMETER resp: ");
    Serial.println(resp.length() ? resp : "(none)");
  }

  Serial.println("[RADIO] Setting ADDRESS...");
  serial_.println("AT+ADDRESS=" + String(DeviceSettings::kRadioNodeAddress));
  delay(500);
  {
    String resp;
    while (serial_.available()) resp += (char)serial_.read();
    Serial.print("[RADIO] ADDRESS resp: ");
    Serial.println(resp.length() ? resp : "(none)");
  }

  Serial.printf("[RADIO] UID=%08X\n", my_uid_);

  Serial.println("[RADIO] Verifying settings...");
  delay(200);

  serial_.println("AT+BAND?");
  delay(200);
  {
    String resp;
    while (serial_.available()) resp += (char)serial_.read();
    Serial.print("[RADIO] BAND? -> ");
    Serial.println(resp.length() ? resp : "(none)");
  }

  serial_.println("AT+NETWORKID?");
  delay(200);
  {
    String resp;
    while (serial_.available()) resp += (char)serial_.read();
    Serial.print("[RADIO] NETWORKID? -> ");
    Serial.println(resp.length() ? resp : "(none)");
  }

  serial_.println("AT+ADDRESS?");
  delay(200);
  {
    String resp;
    while (serial_.available()) resp += (char)serial_.read();
    Serial.print("[RADIO] ADDRESS? -> ");
    Serial.println(resp.length() ? resp : "(none)");
  }

  serial_.println("AT+PARAMETER?");
  delay(200);
  {
    String resp;
    while (serial_.available()) resp += (char)serial_.read();
    Serial.print("[RADIO] PARAMETER? -> ");
    Serial.println(resp.length() ? resp : "(none)");
  }

  Serial.println("[RADIO] Init complete.");
  initialized_ = true;
  return true;
}

bool Rylr998RadioAdapter::sendMessage(const String& message, uint8_t hops, uint32_t msg_id) {
  if (msg_id == 0) msg_id = ++msg_seq_;
  markMessageSeen(my_uid_, msg_id);
  return sendWithAck("M", msg_id, hops, message);
}

bool Rylr998RadioAdapter::sendAlert(const String& alert, uint8_t hops, uint32_t msg_id) {
  if (msg_id == 0) msg_id = ++msg_seq_;
  markMessageSeen(my_uid_, msg_id);
  return sendWithAck("A", msg_id, hops, alert);
}

bool Rylr998RadioAdapter::sendWithAck(const String& type, uint32_t msg_id, uint8_t hops, const String& payload) {
  acked_msg_id_ = 0;
  const uint32_t retry_ms = DeviceSettings::kMeshAckRetryMs;
  const uint32_t max_retry_timer_ms = DeviceSettings::kMeshAckMaxRetryTimerMs;
  const uint32_t window_start = millis();

  while ((millis() - window_start) < max_retry_timer_ms) {
    if (!sendRawMeshMessage(type, my_uid_, 0xFFFFFFFF, msg_id, hops, payload)) {
      delay(10);
      continue;
    }

    const uint32_t wait_start = millis();
    while ((millis() - wait_start) < retry_ms && (millis() - window_start) < max_retry_timer_ms) {
      poll();
      if (acked_msg_id_ == msg_id) {
        return true;
      }
      delay(10);
    }
  }

  return false;
}

bool Rylr998RadioAdapter::sendRawMeshMessage(const String& type, uint32_t sender_uid, uint32_t dest_uid, uint32_t msg_id, uint8_t ttl, const String& payload) {
  // Format: <type>|<sender_uid_hex>|<dest_uid_hex>|<msg_id>|<ttl>|<payload>
  String wire = type + "|" + 
                String(sender_uid, HEX) + "|" + 
                String(dest_uid, HEX) + "|" + 
                String(msg_id) + "|" + 
                String(ttl) + "|" + 
                payload;
  return sendPayload(wire);
}

bool Rylr998RadioAdapter::sendPayload(const String& payload) {
  if (payload.length() == 0) {
    return false;
  }

  // Always send to 0 (RYLR998 broadcast) for mesh routing
  const String cmd =
      "AT+SEND=" + String(DeviceSettings::kMeshBroadcastAddress) + "," + String(payload.length()) + "," + payload;
  serial_.println(cmd);
  return true;
}

void Rylr998RadioAdapter::scheduleRelay(const String& type, uint32_t sender_uid, uint32_t dest_uid, uint32_t msg_id, uint8_t ttl, const String& payload) {
  for (size_t i = 0; i < 5; ++i) {
    if (!relay_jobs_[i].active) {
      relay_jobs_[i].active = true;
      uint32_t jitter = random(DeviceSettings::kMeshJitterMinMs, DeviceSettings::kMeshJitterMaxMs + 1);
      relay_jobs_[i].fire_time_ms = millis() + jitter;
      relay_jobs_[i].type = type;
      relay_jobs_[i].sender_uid = sender_uid;
      relay_jobs_[i].dest_uid = dest_uid;
      relay_jobs_[i].msg_id = msg_id;
      relay_jobs_[i].ttl = ttl;
      relay_jobs_[i].payload = payload;
      Serial.printf("[MESH] RELAY ttl=%d msg=%lu delay=%lums\n",
          ttl, (unsigned long)msg_id, (unsigned long)(relay_jobs_[i].fire_time_ms - millis()));
      break;
    }
  }
}

void Rylr998RadioAdapter::cancelRelayIfMatches(uint32_t sender_uid, uint32_t msg_id) {
  for (size_t i = 0; i < 5; ++i) {
    if (relay_jobs_[i].active && relay_jobs_[i].sender_uid == sender_uid && relay_jobs_[i].msg_id == msg_id) {
      relay_jobs_[i].active = false;
    }
  }
}

void Rylr998RadioAdapter::processRelayJobs() {
  uint32_t now = millis();
  for (size_t i = 0; i < 5; ++i) {
    if (relay_jobs_[i].active && now >= relay_jobs_[i].fire_time_ms) {
      relay_jobs_[i].active = false;
      Serial.printf("[MESH] RELAY-fire ttl=%d msg=%lu\n",
          relay_jobs_[i].ttl, (unsigned long)relay_jobs_[i].msg_id);
      sendRawMeshMessage(
          relay_jobs_[i].type == "M" ? "R" : relay_jobs_[i].type, // Turn M into R for relay, leave A as is
          relay_jobs_[i].sender_uid,
          relay_jobs_[i].dest_uid,
          relay_jobs_[i].msg_id,
          relay_jobs_[i].ttl,
          relay_jobs_[i].payload);
    }
  }
}

void Rylr998RadioAdapter::sendDirectedAck(uint32_t dest_uid, uint32_t msg_id) {
  sendRawMeshMessage("K", my_uid_, dest_uid, msg_id, DeviceSettings::kMeshMaxHops, "");
}

void Rylr998RadioAdapter::poll() {
  processRelayJobs();

  static uint32_t last_debug_ms = 0;
  const uint32_t now = millis();
  if (now - last_debug_ms > 5000) {
    Serial.printf("[RADIO] poll: serial.available=%d\n", serial_.available());
    last_debug_ms = now;
  }

  while (serial_.available() > 0) {
    const char ch = static_cast<char>(serial_.read());

    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      if (line_length_ > 0) {
        line_buffer_[line_length_] = '\0';
        Serial.printf("[RADIO] raw line: %s\n", line_buffer_);
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

  // +RCV=ADDRESS,LENGTH,DATA,RSSI,SNR
  const int firstComma = line.indexOf(',');
  if (firstComma < 0) return;
  const int secondComma = line.indexOf(',', firstComma + 1);
  if (secondComma < 0) return;

  // Find RSSI/SNR commas from the right to allow DATA to contain commas
  const int lastComma = line.lastIndexOf(',');
  if (lastComma < 0 || lastComma <= secondComma) return;
  const int thirdComma = line.lastIndexOf(',', lastComma - 1);
  if (thirdComma < 0 || thirdComma <= secondComma) return;

  const String wirePayload = line.substring(secondComma + 1, thirdComma);

  // Parse Mesh Format: <type>|<sender_uid_hex>|<dest_uid_hex>|<msg_id>|<ttl>|<payload>
  int p1 = wirePayload.indexOf('|');
  if (p1 < 0) return;
  int p2 = wirePayload.indexOf('|', p1 + 1);
  if (p2 < 0) return;
  int p3 = wirePayload.indexOf('|', p2 + 1);
  if (p3 < 0) return;
  int p4 = wirePayload.indexOf('|', p3 + 1);
  if (p4 < 0) return;
  int p5 = wirePayload.indexOf('|', p4 + 1);
  if (p5 < 0) return;

  String type_str = wirePayload.substring(0, p1);
  uint32_t sender_uid = strtoul(wirePayload.substring(p1 + 1, p2).c_str(), NULL, 16);
  uint32_t dest_uid = strtoul(wirePayload.substring(p2 + 1, p3).c_str(), NULL, 16);
  uint32_t msg_id = wirePayload.substring(p3 + 1, p4).toInt();
  uint8_t ttl = wirePayload.substring(p4 + 1, p5).toInt();
  String payload = wirePayload.substring(p5 + 1);

  // Ignore our own reflections
  if (sender_uid == my_uid_) {
    return;
  }

  if (type_str == "K") {
    if (dest_uid == my_uid_) {
      acked_msg_id_ = msg_id;
      Serial.printf("[MESH] ACK from=%08X msg=%lu\n", sender_uid, (unsigned long)msg_id);
    }
    return;
  }

  // Implicit ACK / Collision avoidance: If we hear someone else relaying this exact msg, cancel our pending relay
  cancelRelayIfMatches(sender_uid, msg_id);

  // Deduplication: Ignore if we have seen this (sender, msg_id) recently
  if (isMessageSeen(sender_uid, msg_id)) {
    return;
  }

  markMessageSeen(sender_uid, msg_id);

  message_history_[history_head_].payload.clear();
  message_history_[history_head_] = MeshMessageEntry{
    sender_uid, msg_id, ttl, payload, millis()
  };
  history_head_ = (history_head_ + 1) % DeviceSettings::kMeshHistorySize;
  if (history_count_ < DeviceSettings::kMeshHistorySize) history_count_++;
  total_rx_++;

  Serial.printf("[MESH] RX from=%08X ttl=%d msg=%lu \"%s\"\n",
      sender_uid, ttl, (unsigned long)msg_id, payload.c_str());

  // Is it for us? Add to display
  if (dest_uid == 0xFFFFFFFF || dest_uid == my_uid_) {
    notifyMessageReceived(payload, ttl, msg_id);
  }

  // Should we forward?
  if (ttl > 0 && dest_uid != my_uid_) {
    scheduleRelay(type_str, sender_uid, dest_uid, msg_id, ttl - 1, payload);
  }

  // ARQ end-to-end ACK explicit reply
  if (type_str != "K") {
    sendDirectedAck(sender_uid, msg_id);
  }
}

void Rylr998RadioAdapter::getRecentMessages(MeshMessageEntry* out, size_t max, size_t& count) {
  count = history_count_ < max ? history_count_ : max;
  for (size_t i = 0; i < count; ++i) {
    size_t src = (history_head_ + DeviceSettings::kMeshHistorySize - history_count_ + i) % DeviceSettings::kMeshHistorySize;
    out[i] = message_history_[src];
  }
}

void Rylr998RadioAdapter::getMeshStats(uint32_t& total_rx, size_t& cache_size, size_t& relay_jobs) const {
  total_rx = total_rx_;
  cache_size = 0;
  for (size_t i = 0; i < DeviceSettings::kMeshCacheSize; ++i) {
    if (cache_[i].sender_uid != 0 || cache_[i].msg_id != 0) cache_size++;
  }
  relay_jobs = 0;
  for (size_t i = 0; i < 5; ++i) {
    if (relay_jobs_[i].active) relay_jobs++;
  }
}