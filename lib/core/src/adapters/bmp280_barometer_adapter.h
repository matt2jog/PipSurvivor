#ifndef PIPSURVIVOR_BMP280_BAROMETER_ADAPTER_H
#define PIPSURVIVOR_BMP280_BAROMETER_ADAPTER_H

#include <Arduino.h>
#include <Wire.h>

#include <Adafruit_BMP280.h>

#include "../ports/barometer_port.h"

class Bmp280BarometerAdapter : public BarometerPort {
 public:
  static const uint8_t kDefaultI2cAddress = 0x76;

  explicit Bmp280BarometerAdapter(TwoWire& wire = Wire, uint8_t i2cAddress = kDefaultI2cAddress);

  bool begin() override;
  bool readSample(BarometerSample& sample) override;

  void configureDefaultSampling();

 private:
  TwoWire& wire_;
  uint8_t i2c_address_;
  Adafruit_BMP280 bmp_;
  bool initialized_;
};

#endif