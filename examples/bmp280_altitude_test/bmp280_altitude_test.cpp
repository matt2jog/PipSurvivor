#include <Arduino.h>

#include "ports/barometer_port.h"
#include "ports/altitude_port.h"
#include "adapters/bmp280_barometer_adapter.h"
#include "adapters/barometer_altitude_adapter.h"

// Pull implementation units into this sketch for standalone compile in Arduino IDE.


// Most BMP280 breakout boards are 0x76 or 0x77.
static const uint8_t kBmpAddress = 0x76;

Bmp280BarometerAdapter bmp(Wire, kBmpAddress);
BarometerAltitudeAdapter altitude(bmp);

void setup() {
  Serial.begin(115200);
  delay(200);
  Wire.begin(21, 22);

  Serial.println("[BMP280] init...");
  if (!altitude.begin()) {
    Serial.println("[BMP280] begin failed (check I2C address/wiring)");
    while (true) {
      delay(1000);
    }
  }

  // Update this for local weather pressure to improve altitude estimate.
  bmp.setSeaLevelPressurePa(101325.0f);

  Serial.println("[BMP280] ready");
}

void loop() {
  BarometerSample raw{};
  if (bmp.readSample(raw)) {
    Serial.print("temp C: ");
    Serial.print(raw.temperature_c, 2);
    Serial.print(" | pressure Pa: ");
    Serial.print(raw.pressure_pa, 1);
    Serial.print(" | altitude m: ");
    Serial.println(raw.altitude_m, 2);
  }

  AltitudeSample alt{};
  if (altitude.readAltitude(alt)) {
    Serial.print("derived altitude m: ");
    Serial.print(alt.altitude_m, 2);
    Serial.print(" | vertical speed m/s: ");
    Serial.println(alt.vertical_speed_mps, 3);
  }

  delay(300);
}


