# ESP32 hardware examples

These programs exercise individual PipSurvivor adapters on an ESP32. They are hardware smoke tests, not automated unit tests, and meaningful runtime verification requires the listed devices to be connected.

## Programs

| Directory | Entry point | Behavior | Current PlatformIO environment |
|---|---|---|---|
| `bmp280_altitude_test` | `bmp280_altitude_test.cpp` | Prints BMP280 temperature, pressure, altitude, and altitude-derived vertical speed. | `test_barometer` |
| `sensor_combo_test` | `sensor_combo_test.cpp` | Initializes the MPU6050 accelerometer and gyroscope, BMP280, and water-level adapter, then prints sensor readings. | `test_sensor_combo` |
| `uart_radio_detect` | `main.cpp` | Checks UART loopback and sends `AT` to a RYLR998 on `Serial2`. | `test_uart` |
| `mpu6050_accel_test` | `mpu6050_accel_test.ino` | Prints acceleration and acceleration-derived jerk. | Configured as `test_mpu6050_accel`, but the current external `.ino` source layout produces `Nothing to build`. |
| `mpu6050_gyro_jerk_test` | `mpu6050_gyro_jerk_test.ino` | Prints angular velocity and its time derivative in rad/s². | None. |
| `tzt_water_level_test` | `tzt_water_level_test.ino` | Prints raw/normalized water level and wet state. | Configured as `test_tzt_water_level`, but the current external `.ino` source layout produces `Nothing to build`. |
| `tzt_submersion_test` | `tzt_submersion_test.ino` | Applies threshold, hysteresis, time, and consecutive-sample gates to water readings. | Configured as `test_tzt_submersion`, but the current external `.ino` source layout produces `Nothing to build`. |
| `alert_detection_test` | `alert_detection_test.ino` | Combines gyroscope-derived angular acceleration, altitude change, and submersion into callback alerts with duplicate suppression. | Configured as `test_alert_detection`, but the current external `.ino` source layout produces `Nothing to build`. |

## PlatformIO builds

From the repository root, the `.cpp` examples can be compiled with:

```bash
pio run -e test_barometer
pio run -e test_sensor_combo
pio run -e test_uart
```

Upload a selected environment to a connected compatible board by adding `-t upload`, for example:

```bash
pio run -e test_barometer -t upload
```

Open the serial monitor at the configured 115200 baud:

```bash
pio device monitor -b 115200
```

The `.ino` files are outside PlatformIO's normal `src` layout. The corresponding environments in `platformio.ini` do not currently compile them. To run one, first place or convert it into a valid PlatformIO source layout while preserving access to `lib/core/src`; simply invoking the configured environment is not sufficient.

Although the `.ino` sketches include PipSurvivor `.cpp` implementation files directly, their `ports/...` and `adapters/...` includes still require `lib/core/src` on the compiler include path. Opening a sketch directly in Arduino IDE without packaging/copying that local library tree will not resolve those includes.

## Dependencies

`platformio.ini` resolves these libraries for PlatformIO builds:

- Adafruit BMP280 Library
- Adafruit Unified Sensor
- Adafruit MPU6050

Equivalent libraries and the ESP32 Arduino core are required if an example is moved to another build system.

## Pins used by the examples

| Device | Pins |
|---|---|
| MPU6050 I2C | SDA `21`, SCL `22` |
| BMP280 I2C | SDA `21`, SCL `22`; the barometer-only example uses address `0x76` |
| Water-level analog output | GPIO `34` |
| RYLR998 UART diagnostic | ESP32 RX `16`, TX `17` |

Confirm voltage, logic-level, and power requirements against the module and board documentation before wiring. The examples use 115200 baud for Serial output.

## Submersion configuration demonstrated

`tzt_submersion_test` constructs `WaterLevelSubmersionAdapter` with:

- Enter threshold: normalized value `0.25`
- Exit threshold: normalized value `0.15`
- Minimum wet interval: 1200 ms
- Minimum dry interval: 800 ms
- Minimum consecutive samples: 3
- Evidence mode: `Either` (normalized threshold or wet flag)

These are demonstration settings, not validated drowning or water-ingress safety thresholds.
