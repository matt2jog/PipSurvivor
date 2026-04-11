#include "display_port.h"

namespace {

String normalizeLineForWidth(const String& text, uint8_t width) {
  String out = text;
  if (out.length() > width) {
    out = out.substring(0, width);
  }
  while (out.length() < width) {
    out += ' ';
  }
  return out;
}

String windowText(const String& text, size_t start, uint8_t width) {
  if (text.length() == 0) {
    return normalizeLineForWidth("", width);
  }

  const size_t maxStart = text.length() > 0 ? text.length() - 1 : 0;
  if (start > maxStart) {
    start = maxStart;
  }

  return normalizeLineForWidth(text.substring(start, start + width), width);
}

bool isNumericKey(KeyInput key) {
  return key >= KeyInput::K0 && key <= KeyInput::K9;
}

}  // namespace

DisplayPort::DisplayPort(uint8_t width, uint8_t height)
    : width_(width), height_(height) {}

void DisplayPort::clear() {
  renderFrame(DisplayFrame{"", ""});
}

uint8_t DisplayPort::width() const {
  return width_;
}

uint8_t DisplayPort::height() const {
  return height_;
}

String DisplayPort::normalizeLine(const String& line) const {
  return normalizeLineForWidth(line, width_);
}

TransitionResult transitionState(
    DisplayState currentState,
    KeyInput input,
    DisplayState previousState) {
  if (input == KeyInput::Sys && currentState != DisplayState::Error) {
    return TransitionResult{DisplayState::Error, true};
  }

  if (currentState == DisplayState::Error && input == KeyInput::Ack) {
    return TransitionResult{previousState, previousState != DisplayState::Error};
  }

  switch (currentState) {
    case DisplayState::Initial:
      if (input == KeyInput::A) {
        return TransitionResult{DisplayState::Menu, true};
      }
      if (input == KeyInput::Star || input == KeyInput::Hash || input == KeyInput::B ||
          input == KeyInput::C) {
        return TransitionResult{DisplayState::Initial, false};
      }
      return TransitionResult{DisplayState::Initial, false};

    case DisplayState::Menu:
      if (input == KeyInput::A) {
        return TransitionResult{DisplayState::Initial, true};
      }
      if (input == KeyInput::B) {
        return TransitionResult{DisplayState::Compose, true};
      }
      return TransitionResult{DisplayState::Menu, false};

    case DisplayState::Compose:
      if (input == KeyInput::A) {
        return TransitionResult{DisplayState::Menu, true};
      }
      if (input == KeyInput::C) {
        return TransitionResult{DisplayState::Initial, true};
      }
      if (input == KeyInput::B || input == KeyInput::D || isNumericKey(input)) {
        return TransitionResult{DisplayState::Compose, false};
      }
      return TransitionResult{DisplayState::Compose, false};

    case DisplayState::Error:
      return TransitionResult{DisplayState::Error, false};
  }

  return TransitionResult{currentState, false};
}

DisplayFrame buildInitialFrame(
    const String* messages,
    size_t messageCount,
    size_t messageIndex,
    size_t textOffset,
    uint8_t width) {
  if (messages == nullptr || messageCount == 0) {
    return DisplayFrame{
        normalizeLineForWidth("No messages", width),
        normalizeLineForWidth("A:Menu", width),
    };
  }

  if (messageIndex >= messageCount) {
    messageIndex = messageCount - 1;
  }

  const String selected = messages[messageIndex];
  const String line1 = windowText(selected, textOffset, width);
  String line2 = String(messageIndex + 1) + "/" + String(messageCount) + " A:Menu";
  return DisplayFrame{line1, normalizeLineForWidth(line2, width)};
}

DisplayFrame buildMenuFrame(uint8_t width) {
  return DisplayFrame{
      normalizeLineForWidth("A:Inbox B:New", width),
      normalizeLineForWidth("Select action", width),
  };
}

DisplayFrame buildComposeFrame(const String& draft, size_t cursorOffset, uint8_t width) {
  return DisplayFrame{
      windowText(draft, cursorOffset, width),
      normalizeLineForWidth("A:Back C:Send", width),
  };
}

DisplayFrame buildErrorFrame(const String& errorMessage, uint8_t width) {
  return DisplayFrame{
      normalizeLineForWidth("Error", width),
      normalizeLineForWidth(errorMessage, width),
  };
}