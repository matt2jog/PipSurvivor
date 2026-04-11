#ifndef PIPSURVIVOR_SETTINGS_H
#define PIPSURVIVOR_SETTINGS_H

#include <Arduino.h>

#include "adapters/water_level_submersion_adapter.h"
#include "ports/alert_detection_port.h"
#include "ports/radio_port.h"

namespace DeviceSettings {

// Role: set via build flag -DDEVICE_IS_SENDER=1 or 0
static constexpr bool SENDER = DEVICE_IS_SENDER;

// ============================================================
// Component enable flags.
// Set to true only when the hardware is physically connected.
// ============================================================

// Radio: true enables RYLR998/ESPNow TX+RX, false uses Mock (no-op).
// Note: radio adapter type is still selected by PIPSURVIVOR_RADIO_* build flag.
static constexpr bool kEnableRadio = true;

// MPU6050 gyroscope for jerk/impact detection.
static constexpr bool kEnableGyroscope = false;

// BMP280 barometer for altitude change detection.
static constexpr bool kEnableBarometer = false;

// Analog/digital water level sensor for submersion detection.
static constexpr bool kEnableWaterSensor = false;

// 4x4 matrix button panel for UI input.
static constexpr bool kEnableButtons = false;

// LCD display output (set false to use serial-mirror only).
static constexpr bool kEnableDisplay = false;

// Alert detection subsystem (jerk + altitude + submersion).
// Automatically skips any sensor whose kEnable* flag is false.
static constexpr bool kEnableAlertDetection = kEnableGyroscope || kEnableBarometer || kEnableWaterSensor;

// Radio frequency and network parameters (RYLR998).
static constexpr uint32_t kRadioBand = 915000000;
static constexpr uint8_t kRadioNetworkId = 18;
static constexpr uint8_t kRadioSf = 9;
static constexpr uint8_t kRadioCr = 7;
static constexpr uint8_t kRadioBw = 1;
static constexpr uint8_t kRadioPreamble = 12;
static constexpr uint16_t kRadioAddressSender = 1;
static constexpr uint16_t kRadioAddressReceiver = 2;
static constexpr uint32_t kPingIntervalMs = 2000;

// Number of message entries retained in the on-device inbox/feed.
static constexpr size_t kMaxFeedMessages = 24;

// Maximum queued alerts waiting to be shown and forwarded over radio.
static constexpr size_t kMaxQueuedAlerts = 8;

// Safety cap for compose-mode message length.
static constexpr size_t kMaxDraftLength = 160;

// UI repaint cadence for the 2x16 LCD.
static constexpr uint32_t kDisplayRefreshMs = 120;

// Sampling cadence for alert detection polling.
static constexpr uint32_t kAlertPollMs = 250;

// Multi-tap timeout window for keypad text entry.
static constexpr uint32_t kMultiTapTimeoutMs = 900;

// 4x4 keypad wiring (rows then columns).
static constexpr uint8_t kButtonRows[4] = {32, 33, 25, 26};
static constexpr uint8_t kButtonCols[4] = {27, 14, 12, 13};

// I2C pins for barometer/LCD etc.
static constexpr uint8_t kI2cPinSda = 21;
static constexpr uint8_t kI2cPinScl = 22;

// Debounce filter duration for keypad edge detection.
static constexpr uint16_t kButtonDebounceMs = 35;

// Water sensor pins and calibration defaults.
static constexpr uint8_t kWaterSensorAnalogPin = 34;
static constexpr int kWaterSensorDigitalPin = -1;
static constexpr int kWaterSensorPowerPin = -1;
static constexpr uint16_t kWaterSensorDryRaw = 0;
static constexpr uint16_t kWaterSensorWetRaw = 1023;
static constexpr bool kWaterSensorDigitalWetHigh = true;
static constexpr uint16_t kWaterSensorWetThresholdRaw = 120;
static constexpr uint16_t kWaterSensorSettleDelayMs = 12;

// Radio hardware pins (e.g. RYLR998).
static constexpr int8_t kRadioRxPin = 16;
static constexpr int8_t kRadioTxPin = 17;

// Build radio identity used in outgoing packets/metadata.
inline RadioConfig buildRadioConfig() {
  RadioConfig cfg{};
  cfg.deviceType = "generic-radio";
  cfg.identifier = "device-01";
  return cfg;
}

// Submersion stability policy (hysteresis + debounce windows).
inline SubmersionDetectionConfig buildSubmersionConfig() {
  SubmersionDetectionConfig cfg = WaterLevelSubmersionAdapter::defaultConfig();
  cfg.enter_threshold_normalized = 0.25f;
  cfg.exit_threshold_normalized = 0.15f;
  cfg.min_submersion_time_ms = 1200;
  cfg.min_dry_time_ms = 800;
  cfg.min_consecutive_samples = 3;
  cfg.evidence_mode = SubmersionEvidenceMode::Either;
  return cfg;
}

// Alert acceptance policy for jerk, altitude delta, and submersion constraints.
inline AlertDetectionMetaParams buildAlertMetaParams() {
  AlertDetectionMetaParams params = AlertDetectionMetaParams{
      true,
      18.0f,
      true,
      1.2f,
      true,
      false,
      4000,
      0.4f,
      0.1f,
  };
  return params;
}

}  // namespace DeviceSettings

#endif