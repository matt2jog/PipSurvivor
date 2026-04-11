#ifndef PIPSURVIVOR_MATRIX_BUTTON_PANEL_ADAPTER_H
#define PIPSURVIVOR_MATRIX_BUTTON_PANEL_ADAPTER_H

#include <Arduino.h>

#include "../ports/button_panel_port.h"

class MatrixButtonPanelAdapter : public ButtonPanelPort {
 public:
  static const uint8_t kRows = 4;
  static const uint8_t kCols = 4;

  MatrixButtonPanelAdapter(
      const uint8_t rowPins[kRows],
      const uint8_t colPins[kCols],
      uint16_t debounceMs = 35);

  bool begin() override;
  bool readKey(KeyInput& key) override;

 private:
  KeyInput scanRawKey() const;
  static KeyInput mapKey(uint8_t row, uint8_t col);

  uint8_t row_pins_[kRows];
  uint8_t col_pins_[kCols];
  uint16_t debounce_ms_;

  mutable KeyInput last_raw_;
  mutable KeyInput last_reported_;
  mutable uint32_t last_change_ms_;
};

#endif