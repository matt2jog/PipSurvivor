#include "bmp280_barometer_adapter.h"

#include <math.h>

Bmp280BarometerAdapter::Bmp280BarometerAdapter(TwoWire& wire, uint8_t i2cAddress)
    : BarometerPort(), wire_(wire), i2c_address_(i2cAddress), bmp_(&wire_), initialized_(false) {}

bool Bmp280BarometerAdapter::begin() {
  if (!bmp_.begin(i2c_address_)) {
    initialized_ = false;
    return false;
  }

  configureDefaultSampling();
  initialized_ = true;
  return true;
}

void Bmp280BarometerAdapter::configureDefaultSampling() {
  bmp_.setSampling(
      Adafruit_BMP280::MODE_NORMAL,
      Adafruit_BMP280::SAMPLING_X2,
      Adafruit_BMP280::SAMPLING_X16,
      Adafruit_BMP280::FILTER_X16,
      Adafruit_BMP280::STANDBY_MS_125);
}

bool Bmp280BarometerAdapter::readSample(BarometerSample& sample) {
  if (!initialized_) {
    return false;
  }

  const float temperature = bmp_.readTemperature();
  const float pressure = bmp_.readPressure();
  const float altitude = bmp_.readAltitude(seaLevelPressurePa());

  if (isnan(temperature) || isnan(pressure) || isnan(altitude)) {
    return false;
  }

  sample.temperature_c = temperature;
  sample.pressure_pa = pressure;
  sample.altitude_m = altitude;
  sample.timestamp_ms = millis();
  return true;
}