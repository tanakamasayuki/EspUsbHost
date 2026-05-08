#include "EspUsbHostHid.h"

#include <class/hid/hid.h>

static constexpr uint8_t MOD_LEFT_SHIFT = 0x02;
static constexpr uint8_t MOD_RIGHT_SHIFT = 0x20;

static const uint8_t KEYCODE_TO_ASCII_US[128][2] = { HID_KEYCODE_TO_ASCII };

static const uint8_t KEYCODE_TO_ASCII_JP[128][2] = {
  {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {'a', 'A'}, {'b', 'B'}, {'c', 'C'}, {'d', 'D'}, {'e', 'E'}, {'f', 'F'},
  {'g', 'G'}, {'h', 'H'}, {'i', 'I'}, {'j', 'J'}, {'k', 'K'}, {'l', 'L'},
  {'m', 'M'}, {'n', 'N'}, {'o', 'O'}, {'p', 'P'}, {'q', 'Q'}, {'r', 'R'},
  {'s', 'S'}, {'t', 'T'}, {'u', 'U'}, {'v', 'V'}, {'w', 'W'}, {'x', 'X'},
  {'y', 'Y'}, {'z', 'Z'}, {'1', '!'}, {'2', '"'}, {'3', '#'}, {'4', '$'},
  {'5', '%'}, {'6', '&'}, {'7', '\''}, {'8', '('}, {'9', ')'}, {'0', 0},
  {'\r', '\r'}, {'\x1b', '\x1b'}, {'\b', '\b'}, {'\t', '\t'}, {' ', ' '},
  {'-', '='}, {'^', '~'}, {'@', '`'}, {'[', '{'}, {0, 0}, {']', '}'},
  {';', '+'}, {':', '*'}, {0, 0}, {',', '<'}, {'.', '>'}, {'/', '?'},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {'/', '/'}, {'*', '*'}, {'-', '-'}, {'+', '+'}, {'\r', '\r'},
  {'1', 0}, {'2', 0}, {'3', 0}, {'4', 0}, {'5', '5'}, {'6', 0},
  {'7', 0}, {'8', 0}, {'9', 0}, {'0', 0}, {'.', 0}, {0, 0}, {0, 0},
  {0, 0}, {'=', '='},
};

uint8_t espUsbHostKeypadKeycodeToAscii(uint8_t keycode) {
  switch (keycode) {
    case 0x54: return '/';
    case 0x55: return '*';
    case 0x56: return '-';
    case 0x57: return '+';
    case 0x58: return '\r';
    case 0x59: return '1';
    case 0x5a: return '2';
    case 0x5b: return '3';
    case 0x5c: return '4';
    case 0x5d: return '5';
    case 0x5e: return '6';
    case 0x5f: return '7';
    case 0x60: return '8';
    case 0x61: return '9';
    case 0x62: return '0';
    case 0x63: return '.';
    case 0x67: return '=';
    default: return 0;
  }
}

bool espUsbHostIsBootKeyboardReportValid(const uint8_t *data, size_t length) {
  if (!data || length < ESP_USB_HOST_BOOT_KEYBOARD_REPORT_SIZE) {
    return false;
  }
  if (data[1] != 0) {
    return false;
  }
  for (size_t i = 2; i < ESP_USB_HOST_BOOT_KEYBOARD_REPORT_SIZE; i++) {
    if (data[i] >= 0xe0) {
      return false;
    }
  }
  return true;
}

uint8_t espUsbHostKeycodeToAscii(uint8_t keycode, uint8_t modifiers, EspUsbHostKeyboardLayout layout) {
  if (keycode >= 128) {
    return 0;
  }
  const uint8_t keypadAscii = espUsbHostKeypadKeycodeToAscii(keycode);
  if (keypadAscii) {
    return keypadAscii;
  }

  const uint8_t shift = (modifiers & (MOD_LEFT_SHIFT | MOD_RIGHT_SHIFT)) ? 1 : 0;
  if (layout == ESP_USB_HOST_KEYBOARD_LAYOUT_JP) {
    return KEYCODE_TO_ASCII_JP[keycode][shift];
  }
  return KEYCODE_TO_ASCII_US[keycode][shift];
}

bool espUsbHostParseBootMouseReport(uint8_t interfaceNumber,
                                    const uint8_t *data,
                                    size_t length,
                                    uint8_t previousButtons,
                                    EspUsbHostMouseEvent &event) {
  if (!data || length < 3) {
    return false;
  }

  event = EspUsbHostMouseEvent();
  event.interfaceNumber = interfaceNumber;
  event.previousButtons = previousButtons;
  event.buttons = data[0];
  event.x = static_cast<int8_t>(data[1]);
  event.y = static_cast<int8_t>(data[2]);
  event.wheel = length > 3 ? static_cast<int8_t>(data[3]) : 0;
  event.moved = event.x != 0 || event.y != 0 || event.wheel != 0;
  event.buttonsChanged = event.buttons != event.previousButtons;
  return event.moved || event.buttonsChanged;
}

bool espUsbHostParseConsumerControlReport(uint8_t interfaceNumber,
                                          const uint8_t *data,
                                          size_t length,
                                          uint16_t previousUsage,
                                          EspUsbHostConsumerControlEvent &event) {
  if (!data || length < 2) {
    return false;
  }

  const uint16_t usage = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
  if (usage == previousUsage) {
    return false;
  }

  event = EspUsbHostConsumerControlEvent();
  event.interfaceNumber = interfaceNumber;
  event.usage = usage ? usage : previousUsage;
  event.pressed = usage != 0;
  event.released = usage == 0 && previousUsage != 0;
  return event.pressed || event.released;
}

bool espUsbHostParseGamepadReport(uint8_t interfaceNumber,
                                  const uint8_t *data,
                                  size_t length,
                                  uint32_t previousButtons,
                                  EspUsbHostGamepadEvent &event) {
  if (!data || length < 11) {
    return false;
  }

  event = EspUsbHostGamepadEvent();
  event.interfaceNumber = interfaceNumber;
  event.x = static_cast<int8_t>(data[0]);
  event.y = static_cast<int8_t>(data[1]);
  event.z = static_cast<int8_t>(data[2]);
  event.rz = static_cast<int8_t>(data[3]);
  event.rx = static_cast<int8_t>(data[4]);
  event.ry = static_cast<int8_t>(data[5]);
  event.hat = data[6];
  event.buttons = static_cast<uint32_t>(data[7]) |
                  (static_cast<uint32_t>(data[8]) << 8) |
                  (static_cast<uint32_t>(data[9]) << 16) |
                  (static_cast<uint32_t>(data[10]) << 24);
  event.previousButtons = previousButtons;
  event.changed = event.x != 0 || event.y != 0 || event.z != 0 || event.rz != 0 ||
                  event.rx != 0 || event.ry != 0 || event.hat != 0 ||
                  event.buttons != event.previousButtons;
  return event.changed;
}

bool espUsbHostParseSystemControlReport(uint8_t interfaceNumber,
                                        const uint8_t *data,
                                        size_t length,
                                        uint8_t previousUsage,
                                        EspUsbHostSystemControlEvent &event) {
  if (!data || length < 1) {
    return false;
  }

  const uint8_t usage = data[0];
  if (usage == previousUsage) {
    return false;
  }

  event = EspUsbHostSystemControlEvent();
  event.interfaceNumber = interfaceNumber;
  event.usage = usage ? usage : previousUsage;
  event.pressed = usage != 0;
  event.released = usage == 0 && previousUsage != 0;
  return event.pressed || event.released;
}

uint8_t espUsbHostBuildKeyboardLedReport(bool numLock, bool capsLock, bool scrollLock) {
  uint8_t leds = 0;
  if (numLock) {
    leds |= ESP_USB_HOST_KEYBOARD_LED_NUM_LOCK;
  }
  if (capsLock) {
    leds |= ESP_USB_HOST_KEYBOARD_LED_CAPS_LOCK;
  }
  if (scrollLock) {
    leds |= ESP_USB_HOST_KEYBOARD_LED_SCROLL_LOCK;
  }
  return leds;
}

size_t espUsbHostBuildKeyboardEvents(uint8_t interfaceNumber,
                                     const EspUsbHostKeyboardReport &previousReport,
                                     const EspUsbHostKeyboardReport &currentReport,
                                     EspUsbHostKeyboardLayout layout,
                                     EspUsbHostKeyboardEvent *events,
                                     size_t maxEvents) {
  if (!events || maxEvents == 0) {
    return 0;
  }

  const uint8_t modifiers = currentReport.data[0];
  const uint8_t *keys = currentReport.data + 2;
  const uint8_t *lastKeys = previousReport.data + 2;
  size_t eventCount = 0;

  for (int i = 0; i < 6 && eventCount < maxEvents; i++) {
    const uint8_t key = keys[i];
    if (key == 0) {
      continue;
    }

    bool existed = false;
    for (int j = 0; j < 6; j++) {
      if (lastKeys[j] == key) {
        existed = true;
        break;
      }
    }

    if (!existed) {
      EspUsbHostKeyboardEvent &event = events[eventCount++];
      event.interfaceNumber = interfaceNumber;
      event.pressed = true;
      event.released = false;
      event.keycode = key;
      event.ascii = espUsbHostKeycodeToAscii(key, modifiers, layout);
      event.modifiers = modifiers;
    }
  }

  for (int i = 0; i < 6 && eventCount < maxEvents; i++) {
    const uint8_t key = lastKeys[i];
    if (key == 0) {
      continue;
    }

    bool stillPressed = false;
    for (int j = 0; j < 6; j++) {
      if (keys[j] == key) {
        stillPressed = true;
        break;
      }
    }

    if (!stillPressed) {
      EspUsbHostKeyboardEvent &event = events[eventCount++];
      event.interfaceNumber = interfaceNumber;
      event.pressed = false;
      event.released = true;
      event.keycode = key;
      event.ascii = espUsbHostKeycodeToAscii(key, previousReport.data[0], layout);
      event.modifiers = modifiers;
    }
  }

  return eventCount;
}
