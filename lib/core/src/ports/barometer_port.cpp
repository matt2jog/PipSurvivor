#include "barometer_port.h"

BarometerPort::BarometerPort() : sea_level_pressure_pa_(101325.0f) {}

void BarometerPort::setSeaLevelPressurePa(float seaLevelPressurePa) {
  if (seaLevelPressurePa > 0.0f) {
    sea_level_pressure_pa_ = seaLevelPressurePa;
  }
}

float BarometerPort::seaLevelPressurePa() const {
  return sea_level_pressure_pa_;
}