#include "mock_display_adapter.h"

MockDisplayAdapter::MockDisplayAdapter(uint8_t width, uint8_t height)
    : DisplayPort(width, height),
      last_frame_(DisplayFrame{"", ""}),
      history_count_(0),
      key_head_(0),
      key_tail_(0),
      key_count_(0) {}

void MockDisplayAdapter::renderFrame(const DisplayFrame& frame) {
  last_frame_ = DisplayFrame{normalizeLine(frame.line1), normalizeLine(frame.line2)};

  if (history_count_ < kMaxHistory) {
    history_[history_count_] = last_frame_;
    history_count_++;
    return;
  }

  for (size_t i = 1; i < kMaxHistory; ++i) {
    history_[i - 1] = history_[i];
  }
  history_[kMaxHistory - 1] = last_frame_;
}

bool MockDisplayAdapter::readKey(KeyInput& key) {
  if (key_count_ == 0) {
    key = KeyInput::None;
    return false;
  }

  key = key_buffer_[key_head_];
  key_head_ = (key_head_ + 1) % kMaxKeyBuffer;
  key_count_--;
  return true;
}

bool MockDisplayAdapter::queueKey(KeyInput key) {
  if (key_count_ >= kMaxKeyBuffer) {
    return false;
  }

  key_buffer_[key_tail_] = key;
  key_tail_ = (key_tail_ + 1) % kMaxKeyBuffer;
  key_count_++;
  return true;
}

const DisplayFrame& MockDisplayAdapter::lastFrame() const {
  return last_frame_;
}

size_t MockDisplayAdapter::historyCount() const {
  return history_count_;
}

const DisplayFrame& MockDisplayAdapter::historyAt(size_t index) const {
  if (index >= history_count_) {
    return last_frame_;
  }
  return history_[index];
}