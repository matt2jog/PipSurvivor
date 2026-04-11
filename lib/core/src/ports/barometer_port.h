#ifndef PIPSURVIVOR_BAROMETER_PORT_H
#define PIPSURVIVOR_BAROMETER_PORT_H

#include <Arduino.h>

struct BarometerSample {
  float temperature_c;
  float pressure_pa;
  float altitude_m;
  uint32_t timestamp_ms;
};

class BarometerPort {
 public:
  BarometerPort();
  virtual ~BarometerPort() = default;

  virtual bool begin() = 0;
  virtual bool readSample(BarometerSample& sample) = 0;

  void setSeaLevelPressurePa(float seaLevelPressurePa);
  float seaLevelPressurePa() const;

 protected:
  float sea_level_pressure_pa_;
};

#endif