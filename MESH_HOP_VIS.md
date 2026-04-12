# Mesh Hop Visualization - Implementation Plan

## Problem

The current `RadioReceiveCallback` only passes the raw `String& message` - it loses all mesh metadata (sender UID, TTL/hop count, msg_id). The hop count is parsed in `Rylr998RadioAdapter::handleLine()` but not exposed anywhere.

## Known Issues with Previous Plan

1. **hops_taken is unknowable** - The wire format only carries remaining TTL. Original TTL is not sent, so `hops_taken` cannot be accurately computed. Solution: just expose remaining TTL.
2. **`getDeviceUid()` not on RadioPort** - `main.cpp` holds `RadioPort&`, cannot call `Rylr998RadioAdapter::getDeviceUid()` without casting. Solution: add virtual method to base class.
3. **Stats accessors missing** - `stats: {total_rx, cache_size, relay_jobs}` need accessor methods.
4. **kMeshHistorySize location unspecified** - Put it in the adapter header.

## Solution

### 1. Add MeshMessageEntry struct and virtual methods to RadioPort

**File:** `lib/core/src/ports/radio_port.h`

```cpp
struct MeshMessageEntry {
  uint32_t sender_uid;
  uint32_t msg_id;
  uint8_t ttl;          // remaining TTL when received
  String payload;
  uint32_t recv_time_ms;
};

class RadioPort {
  ...
  virtual uint32_t getDeviceUid() const { return 0; }
  virtual void getRecentMessages(MeshMessageEntry* out, size_t max, size_t& count) { count = 0; }
  virtual void getMeshStats(uint32_t& total_rx, size_t& cache_size, size_t& relay_jobs) const {
    total_rx = 0; cache_size = 0; relay_jobs = 0;
  }
};
```

### 2. Add message history buffer to Rylr998RadioAdapter

**File:** `lib/core/src/adapters/rylr998_radio_adapter.h`

```cpp
static constexpr size_t kMeshHistorySize = 20;

struct MeshMessageEntry {
  uint32_t sender_uid;
  uint32_t msg_id;
  uint8_t ttl;
  String payload;
  uint32_t recv_time_ms;
};

class Rylr998RadioAdapter : public RadioPort {
  ...
  uint32_t getDeviceUid() const override;
  void getRecentMessages(MeshMessageEntry* out, size_t max, size_t& count) override;
  void getMeshStats(uint32_t& total_rx, size_t& cache_size, size_t& relay_jobs) const override;

private:
  MeshMessageEntry message_history_[kMeshHistorySize];
  size_t history_count_;
  size_t history_head_;
  uint32_t total_rx_;
};
```

### 3. Implement history tracking and serial logging in rylr998_radio_adapter.cpp

**File:** `lib/core/src/adapters/rylr998_radio_adapter.cpp`

In `handleLine()`, after parsing and before `notifyMessageReceived()`:
```cpp
// Add to history
size_t idx = history_head_;
message_history_[idx] = MeshMessageEntry{
  sender_uid, msg_id, ttl, payload, millis()
};
history_head_ = (history_head_ + 1) % kMeshHistorySize;
if (history_count_ < kMeshHistorySize) history_count_++;
total_rx_++;

// Serial log
Serial.printf("[MESH] RX from=%08X ttl=%d msg=%lu \"%s\"\n",
    sender_uid, ttl, msg_id, payload.c_str());
```

In `scheduleRelay()`:
```cpp
Serial.printf("[MESH] RELAY ttl=%d msg=%lu delay=%ums\n",
    ttl, msg_id, jitter);
```

In `processRelayJobs()`, on relay fire:
```cpp
Serial.printf("[MESH] RELAY-fire ttl=%d msg=%lu\n",
    relay_jobs_[i].ttl, relay_jobs_[i].msg_id);
```

### 4. Add /mesh endpoint handler

**File:** `src/main.cpp` - add `handleMesh()` function

```cpp
void handleMesh() {
  if (xSemaphoreTake(MainDevice::instance()->radio_mutex_, pdMS_TO_TICKS(50)) == pdFALSE) {
    g_web_server.send(503, "text/plain", "Busy");
    return;
  }

  MeshMessageEntry entries[DeviceSettings::kMeshHistorySize];
  size_t count = 0;
  uint32_t total_rx = 0;
  size_t cache_size = 0;
  size_t relay_jobs = 0;

  g_radio.getRecentMessages(entries, DeviceSettings::kMeshHistorySize, count);
  g_radio.getMeshStats(total_rx, cache_size, relay_jobs);

  uint32_t my_uid = g_radio.getDeviceUid();
  xSemaphoreGive(MainDevice::instance()->radio_mutex_);

  // Build JSON carefully to avoid 32KB String limit
  String json = "{";
  json += "\"my_uid\":\"" + String(my_uid, HEX) + "\",";
  json += "\"messages\":[";

  uint32_t now = millis();
  for (size_t i = 0; i < count; i++) {
    if (i > 0) json += ",";
    json += "{\"sender\":\"" + String(entries[i].sender_uid, HEX) + "\",";
    json += "\"msg_id\":" + String(entries[i].msg_id) + ",";
    json += "\"ttl\":" + String(entries[i].ttl) + ",";
    json += "\"payload\":\"" + escape(entries[i].payload) + "\",";
    json += "\"age_ms\":" + String(now - entries[i].recv_time_ms) + "}";
  }

  json += "],\"stats\":{";
  json += "\"total_rx\":" + String(total_rx) + ",";
  json += "\"cache_size\":" + String(cache_size) + ",";
  json += "\"relay_jobs\":" + String(relay_jobs) + "}}";

  Serial.println("[HTTP] /mesh -> " + json);
  g_web_server.send(200, "application/json", json);
}
```

Register in `initWebServer()`:
```cpp
g_web_server.on("/mesh", handleMesh);
```

## Files to Modify

| File | Changes |
|------|---------|
| `lib/core/src/ports/radio_port.h` | Add `MeshMessageEntry` struct, `getDeviceUid()`, `getRecentMessages()`, `getMeshStats()` virtual methods |
| `lib/core/src/adapters/rylr998_radio_adapter.h` | Add `kMeshHistorySize = 20`, `message_history_[]` buffer, `history_count_`, `history_head_`, `total_rx_`, implement virtual methods |
| `lib/core/src/adapters/rylr998_radio_adapter.cpp` | Initialize history buffer in constructor, populate in `handleLine()`, log in `scheduleRelay()` and `processRelayJobs()`, implement accessor methods |
| `src/main.cpp` | Add `handleMesh()` with mutex lock, `escape()` helper, register `/mesh` in `initWebServer()` |

## JSON Output Example

```json
{
  "my_uid": "a1b2c3d4",
  "messages": [
    {"sender":"e5f60712","msg_id":47,"ttl":5,"payload":"hello world","age_ms":1523},
    {"sender":"a1b2c3d4","msg_id":46,"ttl":7,"payload":"test","age_ms":3847}
  ],
  "stats": {"total_rx":47,"cache_size":50,"relay_jobs":2}
}
```

## Serial Output Format

```
[MESH] RX from=E5F60712 ttl=5 msg=47 "hello world"
[MESH] RELAY ttl=4 msg=47 delay=312ms
[MESH] RELAY-fire ttl=4 msg=47
[MESH] RX from=A1B2C3D4 ttl=7 msg=46 "test"
```

## Critical: Concurrent Access

**Risk:** `poll()` runs in `backgroundReceiverTask` (Core 0) and `g_web_server.handleClient()` runs in the same task but different execution context. If the radio adapter is updating the history buffer while the web server is reading it to generate JSON, the system will crash.

**Fix:** All access to `message_history_[]` and relay job state must be wrapped in the existing `radio_mutex_`.

In `handleMesh()`:
```cpp
if (xSemaphoreTake(MainDevice::instance()->radio_mutex_, pdMS_TO_TICKS(50)) == pdFALSE) {
  g_web_server.send(503, "text/plain", "Busy");
  return;
}
// ... read history and stats ...
xSemaphoreGive(MainDevice::instance()->radio_mutex_);
```

In `handleLine()` in the adapter - no changes needed since it's already called within the mutex:
```cpp
xSemaphoreTake(MainDevice::instance()->radio_mutex_, portMAX_DELAY);
g_radio.poll();
xSemaphoreGive(MainDevice::instance()->radio_mutex_);
```

## Critical: Memory Management

`MeshMessageEntry` stores a `String payload`. If each message is 160 chars and we store 20, that's ~3.2KB of heap - fine for ESP32, but **ensure proper cleanup when the buffer wraps**.

When `history_head_` wraps and overwrites an entry:
```cpp
// Before overwriting, explicitly clear the old String to free memory
message_history_[history_head_].payload.~String();
// Then assign new entry
message_history_[history_head_] = MeshMessageEntry{...};
```

Alternatively, use a circular buffer approach where you always call `clear()` on the String before assignment.

## Critical: JSON Construction

Building JSON by hand with `json += ...` concatenation risks hitting the 32KB String limit on ESP32, especially with 20 messages of up to 160 chars each.

**Mitigations:**
1. Limit `kMeshHistorySize` to 20 - keeps JSON manageable
2. Truncate `payload` in JSON if longer than 128 chars (add `"truncated":true`)
3. Use `escape()` helper to prevent JSON injection
4. Check JSON length before sending, drop oldest entries if needed
5. Consider using ArduinoJson library if available in the project

## Thread Safety Summary

| Operation | Location | Mutex Required? |
|-----------|----------|-----------------|
| `handleLine()` write to history | `rylr998_radio_adapter.cpp` | Yes - called within `poll()` which is wrapped in mutex |
| `processRelayJobs()` write to relay | `rylr998_radio_adapter.cpp` | Yes - called within `poll()` which is wrapped in mutex |
| `getRecentMessages()` read | `main.cpp` `handleMesh()` | Yes - must wrap entire read |
| `getMeshStats()` read | `main.cpp` `handleMesh()` | Yes - must wrap entire read |
| `getDeviceUid()` read | `main.cpp` `handleMesh()` | Yes - must include in mutex block |

The mutex lock/unlock in `handleMesh()` must wrap ALL three calls: `getRecentMessages()`, `getMeshStats()`, and `getDeviceUid()`.