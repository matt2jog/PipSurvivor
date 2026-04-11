#ifndef PIPSURVIVOR_MOCK_DISPLAY_ADAPTER_H
#define PIPSURVIVOR_MOCK_DISPLAY_ADAPTER_H

#include <Arduino.h>

#include "../ports/display_port.h"

class MockDisplayAdapter : public DisplayPort {
 public:
  static const size_t kMaxHistory = 16;
  static const size_t kMaxKeyBuffer = 16;

  MockDisplayAdapter(uint8_t width = 16, uint8_t height = 2);

  void renderFrame(const DisplayFrame& frame) override;
  bool readKey(KeyInput& key) override;
  bool queueKey(KeyInput key);

  const DisplayFrame& lastFrame() const;
  size_t historyCount() const;
  const DisplayFrame& historyAt(size_t index) const;

 private:
  DisplayFrame last_frame_;
  DisplayFrame history_[kMaxHistory];
  size_t history_count_;

  KeyInput key_buffer_[kMaxKeyBuffer];
  size_t key_head_;
  size_t key_tail_;
  size_t key_count_;
};

#endif