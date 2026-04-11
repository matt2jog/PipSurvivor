#ifndef PIPSURVIVOR_DISPLAY_PORT_H
#define PIPSURVIVOR_DISPLAY_PORT_H

#include <Arduino.h>

enum class DisplayState : uint8_t {
  Initial,
  Menu,
  Compose,
  Error
};

enum class KeyInput : uint8_t {
  K0,
  K1,
  K2,
  K3,
  K4,
  K5,
  K6,
  K7,
  K8,
  K9,
  A,
  B,
  C,
  D,
  Star,
  Hash,
  Sys,
  Ack,
  None
};

struct DisplayFrame {
  String line1;
  String line2;
};

struct TransitionResult {
  DisplayState nextState;
  bool changed;
};

class DisplayPort {
 public:
  DisplayPort(uint8_t width = 16, uint8_t height = 2);
  virtual ~DisplayPort() = default;

  virtual void renderFrame(const DisplayFrame& frame) = 0;
  virtual bool readKey(KeyInput& key) = 0;

  void clear();
  uint8_t width() const;
  uint8_t height() const;

 protected:
  String normalizeLine(const String& line) const;

 private:
  uint8_t width_;
  uint8_t height_;
};

TransitionResult transitionState(
    DisplayState currentState,
    KeyInput input,
    DisplayState previousState);

DisplayFrame buildInitialFrame(
    const String* messages,
    size_t messageCount,
    size_t messageIndex,
    size_t textOffset,
    uint8_t width = 16);

DisplayFrame buildMenuFrame(uint8_t width = 16);
DisplayFrame buildComposeFrame(const String& draft, size_t cursorOffset = 0, uint8_t width = 16);
DisplayFrame buildErrorFrame(const String& errorMessage, uint8_t width = 16);

#endif