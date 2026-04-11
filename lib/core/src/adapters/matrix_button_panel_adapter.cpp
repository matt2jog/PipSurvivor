#include "matrix_button_panel_adapter.h"

MatrixButtonPanelAdapter::MatrixButtonPanelAdapter(
    const uint8_t rowPins[kRows],
    const uint8_t colPins[kCols],
    uint16_t debounceMs)
    : ButtonPanelPort(),
      debounce_ms_(debounceMs),
      last_raw_(KeyInput::None),
      last_reported_(KeyInput::None),
      last_change_ms_(0) {
  for (uint8_t i = 0; i < kRows; ++i) {
    row_pins_[i] = rowPins[i];
  }
  for (uint8_t i = 0; i < kCols; ++i) {
    col_pins_[i] = colPins[i];
  }
}

bool MatrixButtonPanelAdapter::begin() {
  for (uint8_t r = 0; r < kRows; ++r) {
    pinMode(row_pins_[r], OUTPUT);
    digitalWrite(row_pins_[r], HIGH);
  }

  for (uint8_t c = 0; c < kCols; ++c) {
    pinMode(col_pins_[c], INPUT_PULLUP);
  }

  last_raw_ = KeyInput::None;
  last_reported_ = KeyInput::None;
  last_change_ms_ = millis();
  return true;
}

KeyInput MatrixButtonPanelAdapter::mapKey(uint8_t row, uint8_t col) {
  // Corrected mapping derived from observed hardware scan values.
  // Physical layout (what's printed on the keycap):
  // [D] [C] [B] [A]
  // [#] [9] [6] [3]
  // [0] [8] [5] [2]
  // [*] [7] [4] [1]
  if (row == 0 && col == 0) return KeyInput::D;
  if (row == 0 && col == 1) return KeyInput::C;
  if (row == 0 && col == 2) return KeyInput::B;
  if (row == 0 && col == 3) return KeyInput::A;

  if (row == 1 && col == 0) return KeyInput::Hash;
  if (row == 1 && col == 1) return KeyInput::K9;
  if (row == 1 && col == 2) return KeyInput::K6;
  if (row == 1 && col == 3) return KeyInput::K3;

  if (row == 2 && col == 0) return KeyInput::K0;
  if (row == 2 && col == 1) return KeyInput::K8;
  if (row == 2 && col == 2) return KeyInput::K5;
  if (row == 2 && col == 3) return KeyInput::K2;

  if (row == 3 && col == 0) return KeyInput::Star;
  if (row == 3 && col == 1) return KeyInput::K7;
  if (row == 3 && col == 2) return KeyInput::K4;
  if (row == 3 && col == 3) return KeyInput::K1;

  return KeyInput::None;
}

KeyInput MatrixButtonPanelAdapter::scanRawKey() const {
  for (uint8_t r = 0; r < kRows; ++r) {
    // Pull one row low at a time, read column lows as active key presses.
    for (uint8_t rr = 0; rr < kRows; ++rr) {
      digitalWrite(row_pins_[rr], rr == r ? LOW : HIGH);
    }

    delayMicroseconds(25);

    for (uint8_t c = 0; c < kCols; ++c) {
      if (digitalRead(col_pins_[c]) == LOW) {
        for (uint8_t rr = 0; rr < kRows; ++rr) {
          digitalWrite(row_pins_[rr], HIGH);
        }
        return mapKey(r, c);
      }
    }
  }

  for (uint8_t rr = 0; rr < kRows; ++rr) {
    digitalWrite(row_pins_[rr], HIGH);
  }

  return KeyInput::None;
}

bool MatrixButtonPanelAdapter::readKey(KeyInput& key) {
  const KeyInput raw = scanRawKey();
  const uint32_t now = millis();

  // Debug: log raw scans periodically to diagnose wiring
  static uint32_t last_raw_debug_ms = 0;
  if ((now - last_raw_debug_ms) >= 500) {
    if (raw != KeyInput::None) {
      Serial.print("[BTN_RAW] scan=");
      Serial.println(static_cast<int>(raw));
    }
    last_raw_debug_ms = now;
  }

  if (raw != last_raw_) {
    last_raw_ = raw;
    last_change_ms_ = now;
  }

  if ((now - last_change_ms_) < debounce_ms_) {
    return false;
  }

  if (raw == KeyInput::None) {
    last_reported_ = KeyInput::None;
    return false;
  }

  if (raw == last_reported_) {
    return false;
  }

  last_reported_ = raw;
  key = raw;
  return true;
}