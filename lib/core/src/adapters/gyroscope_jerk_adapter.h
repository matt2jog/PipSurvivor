#ifndef PIPSURVIVOR_GYROSCOPE_JERK_ADAPTER_H
#define PIPSURVIVOR_GYROSCOPE_JERK_ADAPTER_H

#include <Arduino.h>

#include "../ports/gyroscope_port.h"
#include "../ports/jerk_port.h"

class GyroscopeJerkAdapter : public JerkPort {
 public:
  explicit GyroscopeJerkAdapter(GyroscopePort& gyroscope);

  bool begin() override;
  bool readJerk(AngularJerkSample& sample) override;

  void reset();

 private:
  GyroscopePort& gyroscope_;
  bool has_previous_sample_;
  GyroscopeSample previous_sample_;
};

#endif