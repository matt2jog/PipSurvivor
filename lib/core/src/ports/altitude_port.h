#ifndef PIPSURVIVOR_ALTITUDE_PORT_H
#define PIPSURVIVOR_ALTITUDE_PORT_H

#include <Arduino.h>

struct AltitudeSample {
  float altitude_m;
  float vertical_speed_mps;
  uint32_t timestamp_ms;
};

class AltitudePort {
 public:
  AltitudePort();
  virtual ~AltitudePort() = default;

  virtual bool begin() = 0;
  virtual bool readAltitude(AltitudeSample& sample) = 0;
};

#endif