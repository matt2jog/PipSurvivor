#ifndef PIPSURVIVOR_JERK_PORT_H
#define PIPSURVIVOR_JERK_PORT_H

#include <Arduino.h>

struct AngularJerkSample {
  float jx;
  float jy;
  float jz;
  float magnitude;
  uint32_t timestamp_ms;
};

class JerkPort {
 public:
  JerkPort();
  virtual ~JerkPort() = default;

  virtual bool begin() = 0;
  virtual bool readJerk(AngularJerkSample& sample) = 0;
};

#endif