#ifndef PIPSURVIVOR_TZT_WATER_LEVEL_ADAPTER_H
#define PIPSURVIVOR_TZT_WATER_LEVEL_ADAPTER_H

#include <Arduino.h>

#include "../ports/water_level_port.h"

class TztWaterLevelAdapter : public WaterLevelPort {
 public:
  TztWaterLevelAdapter(
      uint8_t analogPin,
      int digitalPin = -1,
      int powerPin = -1,
      uint16_t dryRaw = 0,
      uint16_t wetRaw = 1023,
      bool digitalWetHigh = true,
      uint16_t wetThresholdRaw = 120,
      uint16_t settleDelayMs = 12);

  bool begin() override;
  bool readLevel(WaterLevelSample& sample) override;

  void setCalibration(uint16_t dryRaw, uint16_t wetRaw);

 private:
  float normalize(uint16_t raw) const;

  uint8_t analog_pin_;
  int digital_pin_;
  int power_pin_;
  uint16_t dry_raw_;
  uint16_t wet_raw_;
  bool digital_wet_high_;
  uint16_t wet_threshold_raw_;
  uint16_t settle_delay_ms_;
};

#endif