#include <Arduino.h>

#include "ports/alert_detection_port.h"
#include "ports/altitude_port.h"
#include "ports/barometer_port.h"
#include "ports/gyroscope_port.h"
#include "ports/jerk_port.h"
#include "ports/submersion_port.h"
#include "ports/water_level_port.h"

#include "adapters/alert_detection_adapter.h"
#include "adapters/barometer_altitude_adapter.h"
#include "adapters/bmp280_barometer_adapter.h"
#include "adapters/gyroscope_jerk_adapter.h"
#include "adapters/mpu6050_gyroscope_adapter.h"
#include "adapters/tzt_water_level_adapter.h"
#include "adapters/water_level_submersion_adapter.h"

// Pull implementation units into this sketch for standalone compile in Arduino IDE.
#include "ports/alert_detection_port.cpp"
#include "ports/altitude_port.cpp"
#include "ports/barometer_port.cpp"
#include "ports/gyroscope_port.cpp"
#include "ports/jerk_port.cpp"
#include "ports/submersion_port.cpp"
#include "ports/water_level_port.cpp"

#include "adapters/alert_detection_adapter.cpp"
#include "adapters/barometer_altitude_adapter.cpp"
#include "adapters/bmp280_barometer_adapter.cpp"
#include "adapters/gyroscope_jerk_adapter.cpp"
#include "adapters/mpu6050_gyroscope_adapter.cpp"
#include "adapters/tzt_water_level_adapter.cpp"
#include "adapters/water_level_submersion_adapter.cpp"

static const uint8_t kBmpAddress = 0x76;
static const uint8_t kWaterAnalogPin = 34;

Mpu6050GyroscopeAdapter gyro;
GyroscopeJerkAdapter jerk(gyro);

Bmp280BarometerAdapter bmp(Wire, kBmpAddress);
BarometerAltitudeAdapter altitude(bmp);

TztWaterLevelAdapter levelSensor(kWaterAnalogPin, -1, -1, 0, 1023, true, 120, 12);
WaterLevelSubmersionAdapter submersion(levelSensor, WaterLevelSubmersionAdapter::defaultConfig());

AlertDetectionMetaParams buildAlertParams() {
  AlertDetectionMetaParams params = AlertDetectionAdapter::defaultMetaParams();

  // Tune these for your use case and platform noise profile.
  params.max_jerk_magnitude = 18.0f;
  params.max_abs_delta_altitude_m = 1.2f;
  params.allow_submersion = false;

  // Duplicate suppression behavior.
  params.duplicate_suppress_ms = 4000;
  params.duplicate_jerk_epsilon = 0.4f;
  params.duplicate_delta_altitude_epsilon_m = 0.1f;
  return params;
}

AlertDetectionAdapter detector(jerk, altitude, submersion, buildAlertParams());

const char* metricName(AlertSensorMetric metric) {
  switch (metric) {
    case AlertSensorMetric::JerkMagnitude:
      return "JerkMagnitude";
    case AlertSensorMetric::DeltaAltitude:
      return "DeltaAltitude";
    case AlertSensorMetric::Submersion:
      return "Submersion";
  }
  return "Unknown";
}

void onAlert(
    const AlertDetectionEvent& event,
    const AlertDetectionCallbackParams& callbackParams) {
  Serial.print("[ALERT] src=");
  Serial.print(callbackParams.source);
  Serial.print(" ch=");
  Serial.print(callbackParams.channel);
  Serial.print(" sev=");
  Serial.print(callbackParams.severity);
  Serial.print(" metric=");
  Serial.print(metricName(event.metric));
  Serial.print(" observed=");
  Serial.print(event.observed_value, 4);
  Serial.print(" threshold=");
  Serial.println(event.threshold_value, 4);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  AlertDetectionCallbackParams callbackParams{};
  callbackParams.source = "esp32-node";
  callbackParams.channel = "serial-debug";
  callbackParams.severity = 2;
  detector.setCallback(onAlert, callbackParams);

  Serial.println("[ALERT_DETECTION] init...");
  const bool started = detector.begin();
  Serial.print("[ALERT_DETECTION] begin status: ");
  Serial.println(started ? "OK" : "PARTIAL/FAIL");
}

void loop() {
  AlertDetectionReading reading{};
  if (detector.poll(reading)) {
    Serial.print("sample: ");
    if (reading.has_jerk) {
      Serial.print("jerk=");
      Serial.print(reading.jerk_magnitude, 3);
      Serial.print(" ");
    }
    if (reading.has_delta_altitude) {
      Serial.print("d_alt_m=");
      Serial.print(reading.delta_altitude_m, 3);
      Serial.print(" ");
    }
    if (reading.has_submersion) {
      Serial.print("submerged=");
      Serial.print(reading.is_submerged ? "YES" : "NO");
      Serial.print(" ");
    }
    Serial.println();
  } else {
    Serial.println("waiting for valid composite sample...");
  }

  delay(200);
}
