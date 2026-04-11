#ifndef PIPSURVIVOR_BUTTON_PANEL_PORT_H
#define PIPSURVIVOR_BUTTON_PANEL_PORT_H

#include <Arduino.h>

#include "display_port.h"

class ButtonPanelPort {
 public:
  ButtonPanelPort();
  virtual ~ButtonPanelPort() = default;

  virtual bool begin() = 0;
  virtual bool readKey(KeyInput& key) = 0;
};

#endif