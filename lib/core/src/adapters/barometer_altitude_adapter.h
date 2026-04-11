#ifndef PIPSURVIVOR_BAROMETER_ALTITUDE_ADAPTER_H
#define PIPSURVIVOR_BAROMETER_ALTITUDE_ADAPTER_H

#include <Arduino.h>

#include "../ports/altitude_port.h"
#include "../ports/barometer_port.h"

class BarometerAltitudeAdapter : public AltitudePort {
 public:
  explicit BarometerAltitudeAdapter(BarometerPort& barometer);

  bool begin() override;
  bool readAltitude(AltitudeSample& sample) override;

  void reset();

 private:
  BarometerPort& barometer_;
  bool has_previous_altitude_;
  AltitudeSample previous_sample_;
};

#endif