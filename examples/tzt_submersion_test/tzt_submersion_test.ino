#include <Arduino.h>

#include "ports/water_level_port.h"
#include "ports/submersion_port.h"
#include "adapters/tzt_water_level_adapter.h"
#include "adapters/water_level_submersion_adapter.h"

// Pull implementation units into this sketch for standalone compile in Arduino IDE.
#include "ports/water_level_port.cpp"
#include "ports/submersion_port.cpp"
#include "adapters/tzt_water_level_adapter.cpp"
#include "adapters/water_level_submersion_adapter.cpp"

// Sensor pins. Change for your board wiring.
static const uint8_t kAnalogPin = 34;
static const int kDigitalPin = -1;
static const int kPowerPin = -1;

TztWaterLevelAdapter levelSensor(
    kAnalogPin,
    kDigitalPin,
    kPowerPin,
    0,
    1023,
    true,
    120,
    12);

SubmersionDetectionConfig buildConfig() {
  SubmersionDetectionConfig cfg = WaterLevelSubmersionAdapter::defaultConfig();

  // Programmatic policy summary:
  // 1) Enter submerged state only when normalized level crosses a stronger threshold.
  // 2) Exit submerged state only when level drops below a weaker threshold.
  // 3) Require both time and consecutive sample gates to reduce false positives.
  // 4) Combine normalized and wet-flag evidence according to evidence_mode.
  cfg.enter_threshold_normalized = 0.25f;
  cfg.exit_threshold_normalized = 0.15f;
  cfg.min_submersion_time_ms = 1500;
  cfg.min_dry_time_ms = 900;
  cfg.min_consecutive_samples = 4;
  cfg.evidence_mode = SubmersionEvidenceMode::Either;
  return cfg;
}

WaterLevelSubmersionAdapter submersion(levelSensor, buildConfig());

void printConfig(const SubmersionDetectionConfig& cfg) {
  Serial.println("[SUBMERSION] config");
  Serial.print("  enter_threshold_normalized: ");
  Serial.println(cfg.enter_threshold_normalized, 3);
  Serial.print("  exit_threshold_normalized: ");
  Serial.println(cfg.exit_threshold_normalized, 3);
  Serial.print("  min_submersion_time_ms: ");
  Serial.println(cfg.min_submersion_time_ms);
  Serial.print("  min_dry_time_ms: ");
  Serial.println(cfg.min_dry_time_ms);
  Serial.print("  min_consecutive_samples: ");
  Serial.println(cfg.min_consecutive_samples);
  Serial.print("  evidence_mode: ");
  Serial.println(static_cast<int>(cfg.evidence_mode));
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("[SUBMERSION] init...");
  if (!submersion.begin()) {
    Serial.println("[SUBMERSION] begin failed");
    while (true) {
      delay(1000);
    }
  }

  printConfig(submersion.config());
  Serial.println("[SUBMERSION] ready");
}

void loop() {
  SubmersionSample s{};
  if (!submersion.readSubmersion(s)) {
    Serial.println("read failed");
    delay(200);
    return;
  }

  Serial.print("raw=");
  Serial.print(s.raw);
  Serial.print(" norm=");
  Serial.print(s.normalized, 3);
  Serial.print(" candidate=");
  Serial.print(s.candidate_submerged ? "WET" : "DRY");
  Serial.print(" stable=");
  Serial.print(s.submerged ? "SUBMERGED" : "NOT_SUBMERGED");
  Serial.print(" cand_ms=");
  Serial.print(s.candidate_duration_ms);
  Serial.print(" samples=");
  Serial.println(s.consecutive_samples);

  delay(150);
}
