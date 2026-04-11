#ifndef PIPSURVIVOR_WATER_LEVEL_PORT_H
#define PIPSURVIVOR_WATER_LEVEL_PORT_H

#include <Arduino.h>

struct WaterLevelSample {
  uint16_t raw;
  float normalized;
  bool wet;
  uint32_t timestamp_ms;
};

class WaterLevelPort {
 public:
  WaterLevelPort();
  virtual ~WaterLevelPort() = default;

  virtual bool begin() = 0;
  virtual bool readLevel(WaterLevelSample& sample) = 0;

  bool isWet();

 protected:
  bool has_last_sample_;
  WaterLevelSample last_sample_;
};

#endif