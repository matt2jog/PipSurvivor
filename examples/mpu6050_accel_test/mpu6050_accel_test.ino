#include <Arduino.h>

#include "ports/accel_port.h"
#include "adapters/mpu6050_accel_adapter.h"

// Pull implementation units into this sketch for standalone compile in Arduino IDE.
#include "ports/accel_port.cpp"
#include "adapters/mpu6050_accel_adapter.cpp"

Mpu6050AccelAdapter accel;

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("[MPU6050 ACCEL] init...");
  if (!accel.begin()) {
    Serial.println("[MPU6050 ACCEL] begin failed");
    while (true) {
      delay(1000);
    }
  }

  Serial.println("[MPU6050 ACCEL] ready");
}

void loop() {
  LinearAccelerationSample lin{};
  if (accel.readLinearAcceleration(lin)) {
    Serial.print("lin m/s^2: ");
    Serial.print(lin.ax_mps2, 4);
    Serial.print(", ");
    Serial.print(lin.ay_mps2, 4);
    Serial.print(", ");
    Serial.println(lin.az_mps2, 4);
  }

  JerkSample jerk{};
  if (accel.readJerk(jerk)) {
    Serial.print("jerk m/s^3: ");
    Serial.print(jerk.jx_mps3, 4);
    Serial.print(", ");
    Serial.print(jerk.jy_mps3, 4);
    Serial.print(", ");
    Serial.print(jerk.jz_mps3, 4);
    Serial.print(" | mag=");
    Serial.println(jerk.magnitude_mps3, 4);
  } else {
    Serial.println("jerk warming up...");
  }

  delay(100);
}
