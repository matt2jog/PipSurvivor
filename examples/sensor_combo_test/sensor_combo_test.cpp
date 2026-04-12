#include <Arduino.h>
#include <Wire.h>

#include "adapters/bmp280_barometer_adapter.h"
#include "adapters/mpu6050_accel_adapter.h"
#include "adapters/mpu6050_gyroscope_adapter.h"
#include "adapters/tzt_water_level_adapter.h"
#include "settings.h"

// Initialize sensors
TwoWire I2CSensors = TwoWire(0);

using namespace DeviceSettings;

Bmp280BarometerAdapter barometer(I2CSensors);
Mpu6050AccelAdapter accel(I2CSensors);
Mpu6050GyroscopeAdapter gyro(I2CSensors);

TztWaterLevelAdapter waterLevel(
    kWaterSensorAnalogPin,
    kWaterSensorDigitalPin,
    kWaterSensorPowerPin);

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("Starting Sensor Combo Test...");

  // Initialize I2C Bus for the MPU6050
  I2CSensors.begin(kI2cPinSda, kI2cPinScl, 400000);

  // 1. Init Accel (This will also allow us to configure the MPU6050 I2C bypass if needed)
  if (accel.begin()) {
    Serial.println("MPU6050 Accel initialized successfully.");
  } else {
    Serial.println("Failed to find MPU6050 (Accel).");
  }

  if (gyro.begin()) {
    Serial.println("MPU6050 Gyro initialized successfully.");
  } else {
    Serial.println("Failed to find MPU6050 (Gyro).");
  }

  // Allow MPU's bypass to settle
  delay(100);

  // 3. Init BMP280, which is connected to XDA/XCL of the MPU6050
  if (barometer.begin()) {
    Serial.println("BMP280 Barometer initialized successfully.");
  } else {
    Serial.println("Failed to find BMP280 Barometer. Check wiring / I2C bypass.");
  }

  // 4. Init Water Level Sensor
  if (waterLevel.begin()) {
    Serial.println("Water Level Sensor initialized.");
  } else {
    Serial.println("Water Level Sensor begin returned false.");
  }

  Serial.println("Sensors initialized. Beginning loop...");
}

void loop() {
  BarometerSample baroSample;
  LinearAccelerationSample accelSample;
  GyroscopeSample gyroSample;
  WaterLevelSample waterSample;

  if (barometer.readSample(baroSample) &&
      accel.readLinearAcceleration(accelSample) &&
      gyro.readAngularVelocity(gyroSample) &&
      waterLevel.readLevel(waterSample)) {

    // Log Output
    Serial.println("---- Sensor Readings ----");
    Serial.printf("Barometer : Temp: %.2f *C, Pressure: %.2f Pa\n", baroSample.temperature_c, baroSample.pressure_pa);
    Serial.printf("Accel     : %.2f, %.2f, %.2f (m/s^2)\n", accelSample.ax_mps2, accelSample.ay_mps2, accelSample.az_mps2);
    Serial.printf("Gyro      : %.2f, %.2f, %.2f (rad/s)\n", gyroSample.gx_rad_s, gyroSample.gy_rad_s, gyroSample.gz_rad_s);
    Serial.printf("Water Lvl : %.2f %% (Raw: %u)\n", waterSample.normalized * 100.0f, waterSample.raw);
    Serial.println("-------------------------");
  } else {
    Serial.println("Failed to read from one or more sensors.");
  }
  
  delay(1000);
}
