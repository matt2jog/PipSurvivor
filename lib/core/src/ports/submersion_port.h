#ifndef PIPSURVIVOR_SUBMERSION_PORT_H
#define PIPSURVIVOR_SUBMERSION_PORT_H

#include <Arduino.h>

struct SubmersionSample {
  bool submerged;
  bool candidate_submerged;
  uint16_t raw;
  float normalized;
  uint32_t timestamp_ms;
  uint32_t candidate_duration_ms;
  uint16_t consecutive_samples;
};

class SubmersionPort {
 public:
  SubmersionPort();
  virtual ~SubmersionPort() = default;

  virtual bool begin() = 0;
  virtual bool readSubmersion(SubmersionSample& sample) = 0;

  bool isSubmerged();

 protected:
  bool has_last_sample_;
  SubmersionSample last_sample_;
};

#endif