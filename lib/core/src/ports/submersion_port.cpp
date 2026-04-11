#include "submersion_port.h"

SubmersionPort::SubmersionPort()
    : has_last_sample_(false),
      last_sample_(SubmersionSample{false, false, 0, 0.0f, 0, 0, 0}) {}

bool SubmersionPort::isSubmerged() {
  SubmersionSample sample{};
  if (readSubmersion(sample)) {
    last_sample_ = sample;
    has_last_sample_ = true;
  }

  if (!has_last_sample_) {
    return false;
  }

  return last_sample_.submerged;
}