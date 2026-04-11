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
      cache_index_(0) {
  serial_.begin(baudRate);
  memset(line_buffer_, 0, sizeof(line_buffer_));
  
  for (size_t i = 0; i < DeviceSettings::kMeshCacheSize; ++i) {
    cache_[i] = MessageCacheItem{0, 0};
  }
  for (size_t i = 0; i < 5; ++i) {
    relay_jobs_[i].active = false;
  }
  setupUid();
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

  serial_.println("AT+BAND=" + String(DeviceSettings::kRadioBand));
  delay(50);
  serial_.println("AT+NETWORKID=" + String(DeviceSettings::kRadioNetworkId));
  delay(50);
  serial_.println("AT+PARAMETER=" + String(DeviceSettings::kRadioSf) + "," +
                  String(DeviceSettings::kRadioCr) + "," +
                  String(DeviceSettings::kRadioBw) + "," +
                  String(DeviceSettings::kRadioPreamble));
  delay(50);
  serial_.println("AT+ADDRESS=" + String(DeviceSettings::SENDER
      ? DeviceSettings::kRadioAddressSender
      : DeviceSettings::kRadioAddressReceiver));
  delay(500);

  initialized_ = true;
  return true;
}

bool Rylr998RadioAdapter::sendMessage(const String& message, uint8_t hops) {
  uint32_t msg = ++msg_seq_;
  markMessageSeen(my_uid_, msg);
  return sendRawMeshMessage("M", my_uid_, 0xFFFFFFFF, msg, hops, message);
}

bool Rylr998RadioAdapter::sendAlert(const String& alert, uint8_t hops) {
  uint32_t msg = ++msg_seq_;
  markMessageSeen(my_uid_, msg);
  return sendRawMeshMessage("A", my_uid_, 0xFFFFFFFF, msg, hops, alert);
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
  // Not fully implemented for ARQ yet, but this is how we'd reply
  // sendRawMeshMessage("K", my_uid_, dest_uid, msg_id, DeviceSettings::kMeshMaxHops, "");
}

void Rylr998RadioAdapter::poll() {
  processRelayJobs();

  while (serial_.available() > 0) {
    const char ch = static_cast<char>(serial_.read());

    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      if (line_length_ > 0) {
        line_buffer_[line_length_] = '\0';
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

  // Implicit ACK / Collision avoidance: If we hear someone else relaying this exact msg, cancel our pending relay
  cancelRelayIfMatches(sender_uid, msg_id);

  // Deduplication: Ignore if we have seen this (sender, msg_id) recently
  if (isMessageSeen(sender_uid, msg_id)) {
    return;
  }

  markMessageSeen(sender_uid, msg_id);

  // Is it for us? Add to display
  if (dest_uid == 0xFFFFFFFF || dest_uid == my_uid_) {
    notifyMessageReceived(payload);
  }

  // Should we forward?
  if (ttl > 0 && dest_uid != my_uid_) {
    scheduleRelay(type_str, sender_uid, dest_uid, msg_id, ttl - 1, payload);
  }

  // ARQ end-to-end ACK explicit reply
  if (dest_uid == my_uid_ && type_str != "K") {
    sendDirectedAck(sender_uid, msg_id);
  }
}