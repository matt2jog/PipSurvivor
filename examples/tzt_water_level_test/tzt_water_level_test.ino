#include <Arduino.h>

#include "ports/water_level_port.h"
#include "adapters/tzt_water_level_adapter.h"

// Pull implementation units into this sketch for standalone compile in Arduino IDE.
#include "ports/water_level_port.cpp"
#include "adapters/tzt_water_level_adapter.cpp"

// ESP32 ADC input pin for sensor analog output.
static const uint8_t kAnalogPin = 34;

// Optional:
// - digitalPin: use if your module exposes a thresholded digital output
// - powerPin: drive VCC from this pin to reduce probe corrosion during idle
static const int kDigitalPin = -1;
static const int kPowerPin = -1;

TztWaterLevelAdapter water(
    kAnalogPin,
    kDigitalPin,
    kPowerPin,
    0,
    1023,
    true,
    120,
    12);

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("[TZT WATER] init...");
  if (!water.begin()) {
    Serial.println("[TZT WATER] begin failed");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("[TZT WATER] ready");
}

void loop() {
  WaterLevelSample s{};
  if (water.readLevel(s)) {
    Serial.print("raw: ");
    Serial.print(s.raw);
    Serial.print(" | norm: ");
    Serial.print(s.normalized, 3);
    Serial.print(" | wet: ");
    Serial.println(s.wet ? "YES" : "NO");
  }

  delay(250);
}
