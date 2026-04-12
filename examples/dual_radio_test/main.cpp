#include <Arduino.h>
#include <WiFi.h>

#include "../../lib/core/src/adapters/dual_radio_adapter.h"
#include "../../lib/core/src/adapters/espnow_radio_adapter.h"
#include "../../lib/core/src/adapters/mock_radio_adapter.h"
#include "../../lib/core/src/adapters/rylr998_radio_adapter.h"
#include "../../lib/core/src/ports/radio_port.h"
#include "../../lib/core/src/settings.h"

static uint32_t g_rx_count = 0;
static uint32_t g_tx_count = 0;
static String g_last_rx = "";

void onMessageReceived(const String& message, uint8_t hops) {
  g_rx_count++;
  g_last_rx = message;
  Serial.printf("[TEST] RX #%lu hops=%d: \"%s\"\n", (unsigned long)g_rx_count, hops, message.c_str());
}

EspNowRadioAdapter g_espnow(DeviceSettings::buildRadioConfig(), DeviceSettings::kEspNowChannel);
Rylr998RadioAdapter g_rylr(DeviceSettings::buildRadioConfig(), Serial2, DeviceSettings::kMeshBroadcastAddress, 115200);
DualRadioAdapter g_dual(g_rylr, g_espnow);

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[TEST] === Dual Radio Test ===");

  Serial2.begin(115200, SERIAL_8N1, DeviceSettings::kRadioRxPin, DeviceSettings::kRadioTxPin);
  WiFi.mode(WIFI_AP_STA);

  g_dual.setReceiveCallback(onMessageReceived);

  if (!g_dual.begin()) {
    Serial.println("[TEST] FAIL: DualRadioAdapter::begin() returned false");
    return;
  }
  Serial.println("[TEST] Dual radio initialized OK");

  Serial.println("[TEST] Sending test message on both channels...");
  bool ok = g_dual.sendMessage("hello from dual radio", 1);
  g_tx_count++;
  Serial.printf("[TEST] sendMessage result: %d\n", ok);

  Serial.println("[TEST] Sending test alert on both channels...");
  ok = g_dual.sendAlert("TEST ALERT dual radio", 1);
  g_tx_count++;
  Serial.printf("[TEST] sendAlert result: %d\n", ok);

  Serial.println("[TEST] Listening for incoming messages on both channels...");
  Serial.println("[TEST] Send a message via ESP-NOW or RYLR998 to this device.");
  Serial.println("[TEST] Watch for [DUAL_RADIO] and [TEST] RX lines.");
}

void loop() {
  g_dual.poll();

  static uint32_t last_status_ms = 0;
  uint32_t now = millis();
  if (now - last_status_ms > 5000) {
    Serial.printf("[TEST] status: tx=%lu rx=%lu last_rx=\"%s\"\n",
        (unsigned long)g_tx_count, (unsigned long)g_rx_count, g_last_rx.c_str());
    last_status_ms = now;
  }

  delay(10);
}
