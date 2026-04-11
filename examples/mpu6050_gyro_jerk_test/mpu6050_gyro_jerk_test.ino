#include <Arduino.h>

#include "ports/gyroscope_port.h"
#include "ports/jerk_port.h"
#include "adapters/mpu6050_gyroscope_adapter.h"
#include "adapters/gyroscope_jerk_adapter.h"

// Pull implementation units into this sketch for standalone compile in Arduino IDE.
#include "ports/gyroscope_port.cpp"
#include "ports/jerk_port.cpp"
#include "adapters/mpu6050_gyroscope_adapter.cpp"
#include "adapters/gyroscope_jerk_adapter.cpp"

Mpu6050GyroscopeAdapter gyro;
GyroscopeJerkAdapter gyroJerk(gyro);

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("[MPU6050 GYRO] init...");
  if (!gyroJerk.begin()) {
    Serial.println("[MPU6050 GYRO] begin failed");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("[MPU6050 GYRO] ready");
}

void loop() {
  GyroscopeSample g{};
  if (gyro.readAngularVelocity(g)) {
    Serial.print("gyro rad/s: ");
    Serial.print(g.gx_rad_s, 4);
    Serial.print(", ");
    Serial.print(g.gy_rad_s, 4);
    Serial.print(", ");
    Serial.println(g.gz_rad_s, 4);
  }

  AngularJerkSample j{};
  if (gyroJerk.readJerk(j)) {
    Serial.print("ang jerk rad/s^2: ");
    Serial.print(j.jx, 4);
    Serial.print(", ");
    Serial.print(j.jy, 4);
    Serial.print(", ");
    Serial.print(j.jz, 4);
    Serial.print(" | mag=");
    Serial.println(j.magnitude, 4);
  } else {
    Serial.println("ang jerk warming up...");
  }

  delay(100);
}
