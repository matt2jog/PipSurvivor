# ESP32 Sensor Test Suite

This folder contains standalone Arduino sketches to smoke-test each sensor path on ESP32.

## Included Tests

- `mpu6050_accel_test`: Reads linear acceleration and derived jerk from `Mpu6050AccelAdapter`.
- `mpu6050_gyro_jerk_test`: Reads angular velocity and derived angular jerk from `Mpu6050GyroscopeAdapter` + `GyroscopeJerkAdapter`.
- `bmp280_altitude_test`: Reads temperature/pressure/altitude from `Bmp280BarometerAdapter` and vertical speed via `BarometerAltitudeAdapter`.
- `tzt_water_level_test`: Reads raw/normalized water level and wet status from `TztWaterLevelAdapter`.
- `tzt_submersion_test`: Uses `WaterLevelSubmersionAdapter` to decide if the probe is considered submerged with time/sample debouncing.
- `alert_detection_test`: Joins jerk, delta-altitude, and submersion into `AlertDetectionAdapter` and emits de-duplicated alerts via callback.

## Prerequisites

1. ESP32 board package installed in Arduino IDE.
2. Library Manager dependency:
   - `Adafruit BMP280 Library` (for BMP280 test).
3. Hardware connected to ESP32:
   - MPU6050 on I2C (`SDA=21`, `SCL=22` by default on many ESP32 boards)
   - BMP280 on I2C (`SDA=21`, `SCL=22`)
   - TZT Water Level sensor signal to ADC pin (default in test: GPIO34)

## Wiring Notes

### MPU6050

- `VCC -> 3.3V`
- `GND -> GND`
- `SDA -> GPIO21`
- `SCL -> GPIO22`

### BMP280

- `VCC -> 3.3V`
- `GND -> GND`
- `SDA -> GPIO21`
- `SCL -> GPIO22`

### TZT Water Level Sensor

- `S/SIG -> GPIO34` (ADC input)
- `+ -> 3.3V` or power-gated digital pin
- `- -> GND`
- Optional: route `+` through a digital GPIO for pulse-powering (to reduce corrosion)

## How To Run

1. Open one test sketch folder in Arduino IDE.
2. Select your ESP32 board and COM port.
3. Verify and Upload.
4. Open Serial Monitor at `115200` baud.

## Test Sketch Paths

- `tests/mpu6050_accel_test/mpu6050_accel_test.ino`
- `tests/mpu6050_gyro_jerk_test/mpu6050_gyro_jerk_test.ino`
- `tests/bmp280_altitude_test/bmp280_altitude_test.ino`
- `tests/tzt_water_level_test/tzt_water_level_test.ino`
- `tests/tzt_submersion_test/tzt_submersion_test.ino`
- `tests/alert_detection_test/alert_detection_test.ino`

## Notes

- These are hardware smoke tests, not host-side unit tests.
- Each sketch includes required adapter/port `.cpp` files directly to keep tests self-contained.
- Keep MPU6050 still for a few seconds at startup so calibration offsets are stable.
- For BMP280 altitude quality, adjust sea-level pressure in the test sketch for your location.

## Submersion Tuning Notes

`tzt_submersion_test` uses decoupled detection meta-parameters:

- `enter_threshold_normalized`: level needed to start considering submerged.
- `exit_threshold_normalized`: lower level required to exit submerged state.
- `min_submersion_time_ms`: minimum stable wet time before transition to submerged.
- `min_dry_time_ms`: minimum stable dry time before transition to dry.
- `min_consecutive_samples`: minimum uninterrupted samples of the candidate state.
- `evidence_mode`: selects whether normalized level, wet flag, both, or either are used.

This gives a robust state transition for noisy ADC signals and intermittent droplets.

## Alert Detection Notes

`alert_detection_test` demonstrates:

- Decoupled acceptance meta-parameters for jerk, delta altitude, and submersion.
- Callback-based alert dispatch (`AlertDetectionCallback`) with predefined callback params (`source`, `channel`, `severity`).
- Duplicate suppression cache to avoid repeated alerts for near-identical out-of-range values.