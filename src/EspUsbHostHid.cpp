#include "EspUsbHostHid.h"

#include "keymap/keymap_da_dk.h"
#include "keymap/keymap_de_de.h"
#include "keymap/keymap_en_gb.h"
#include "keymap/keymap_en_us.h"
#include "keymap/keymap_es_es.h"
#include "keymap/keymap_fi_fi.h"
#include "keymap/keymap_fr_ch.h"
#include "keymap/keymap_fr_fr.h"
#include "keymap/keymap_hu_hu.h"
#include "keymap/keymap_it_it.h"
#include "keymap/keymap_ja_jp.h"
#include "keymap/keymap_nb_no.h"
#include "keymap/keymap_nl_nl.h"
#include "keymap/keymap_pt_br.h"
#include "keymap/keymap_pt_pt.h"
#include "keymap/keymap_sv_se.h"

static constexpr uint8_t MOD_LEFT_SHIFT = 0x02;
static constexpr uint8_t MOD_RIGHT_SHIFT = 0x20;

uint8_t espUsbHostKeypadKeycodeToAscii(uint8_t keycode)
{
  switch (keycode)
  {
  case 0x54:
    return '/';
  case 0x55:
    return '*';
  case 0x56:
    return '-';
  case 0x57:
    return '+';
  case 0x58:
    return '\r';
  case 0x59:
    return '1';
  case 0x5a:
    return '2';
  case 0x5b:
    return '3';
  case 0x5c:
    return '4';
  case 0x5d:
    return '5';
  case 0x5e:
    return '6';
  case 0x5f:
    return '7';
  case 0x60:
    return '8';
  case 0x61:
    return '9';
  case 0x62:
    return '0';
  case 0x63:
    return '.';
  case 0x67:
    return '=';
  default:
    return 0;
  }
}

bool espUsbHostIsBootKeyboardReportValid(const uint8_t *data, size_t length)
{
  if (!data || length < ESP_USB_HOST_BOOT_KEYBOARD_REPORT_SIZE)
  {
    return false;
  }
  if (data[1] != 0)
  {
    return false;
  }
  for (size_t i = 2; i < ESP_USB_HOST_BOOT_KEYBOARD_REPORT_SIZE; i++)
  {
    if (data[i] >= 0xe0)
    {
      return false;
    }
  }
  return true;
}

uint8_t espUsbHostKeycodeToAscii(uint8_t keycode, uint8_t modifiers, EspUsbHostKeyboardLayout layout)
{
  if (keycode >= 128)
  {
    return 0;
  }
  const uint8_t keypadAscii = espUsbHostKeypadKeycodeToAscii(keycode);
  if (keypadAscii)
  {
    return keypadAscii;
  }

  const uint8_t shift = (modifiers & (MOD_LEFT_SHIFT | MOD_RIGHT_SHIFT)) ? 1 : 0;
  const uint8_t (*table)[2];
  switch (layout)
  {
  case ESP_USB_HOST_KEYBOARD_LAYOUT_DA_DK:
    table = KEYCODE_TO_ASCII_DA_DK;
    break;
  case ESP_USB_HOST_KEYBOARD_LAYOUT_DE_DE:
    table = KEYCODE_TO_ASCII_DE_DE;
    break;
  case ESP_USB_HOST_KEYBOARD_LAYOUT_EN_GB:
    table = KEYCODE_TO_ASCII_EN_GB;
    break;
  case ESP_USB_HOST_KEYBOARD_LAYOUT_ES_ES:
    table = KEYCODE_TO_ASCII_ES_ES;
    break;
  case ESP_USB_HOST_KEYBOARD_LAYOUT_FI_FI:
    table = KEYCODE_TO_ASCII_FI_FI;
    break;
  case ESP_USB_HOST_KEYBOARD_LAYOUT_FR_CH:
    table = KEYCODE_TO_ASCII_FR_CH;
    break;
  case ESP_USB_HOST_KEYBOARD_LAYOUT_FR_FR:
    table = KEYCODE_TO_ASCII_FR_FR;
    break;
  case ESP_USB_HOST_KEYBOARD_LAYOUT_HU_HU:
    table = KEYCODE_TO_ASCII_HU_HU;
    break;
  case ESP_USB_HOST_KEYBOARD_LAYOUT_IT_IT:
    table = KEYCODE_TO_ASCII_IT_IT;
    break;
  case ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP:
    table = KEYCODE_TO_ASCII_JA_JP;
    break;
  case ESP_USB_HOST_KEYBOARD_LAYOUT_NB_NO:
    table = KEYCODE_TO_ASCII_NB_NO;
    break;
  case ESP_USB_HOST_KEYBOARD_LAYOUT_NL_NL:
    table = KEYCODE_TO_ASCII_NL_NL;
    break;
  case ESP_USB_HOST_KEYBOARD_LAYOUT_PT_BR:
    table = KEYCODE_TO_ASCII_PT_BR;
    break;
  case ESP_USB_HOST_KEYBOARD_LAYOUT_PT_PT:
    table = KEYCODE_TO_ASCII_PT_PT;
    break;
  case ESP_USB_HOST_KEYBOARD_LAYOUT_SV_SE:
    table = KEYCODE_TO_ASCII_SV_SE;
    break;
  default:
    table = KEYCODE_TO_ASCII_EN_US;
    break;
  }
  return table[keycode][shift];
}

bool espUsbHostParseBootMouseReport(uint8_t interfaceNumber,
                                    const uint8_t *data,
                                    size_t length,
                                    uint8_t previousButtons,
                                    EspUsbHostMouseEvent &event)
{
  if (!data || length < 3)
  {
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
                                          EspUsbHostConsumerControlEvent &event)
{
  if (!data || length < 2)
  {
    return false;
  }

  const uint16_t usage = static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
  if (usage == previousUsage)
  {
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
                                  const EspUsbHostGamepadPrevState &previous,
                                  EspUsbHostGamepadEvent &event)
{
  if (!data || length < 11)
  {
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
  event.previousButtons = previous.buttons;
  event.changed = event.x != previous.x || event.y != previous.y ||
                  event.z != previous.z || event.rz != previous.rz ||
                  event.rx != previous.rx || event.ry != previous.ry ||
                  event.hat != previous.hat ||
                  event.buttons != previous.buttons;
  return event.changed;
}

bool espUsbHostParseSystemControlReport(uint8_t interfaceNumber,
                                        const uint8_t *data,
                                        size_t length,
                                        uint8_t previousUsage,
                                        EspUsbHostSystemControlEvent &event)
{
  if (!data || length < 1)
  {
    return false;
  }

  const uint8_t usage = data[0];
  if (usage == previousUsage)
  {
    return false;
  }

  event = EspUsbHostSystemControlEvent();
  event.interfaceNumber = interfaceNumber;
  event.usage = usage ? usage : previousUsage;
  event.pressed = usage != 0;
  event.released = usage == 0 && previousUsage != 0;
  return event.pressed || event.released;
}

uint8_t espUsbHostBuildKeyboardLedReport(bool numLock, bool capsLock, bool scrollLock)
{
  uint8_t leds = 0;
  if (numLock)
  {
    leds |= ESP_USB_HOST_KEYBOARD_LED_NUM_LOCK;
  }
  if (capsLock)
  {
    leds |= ESP_USB_HOST_KEYBOARD_LED_CAPS_LOCK;
  }
  if (scrollLock)
  {
    leds |= ESP_USB_HOST_KEYBOARD_LED_SCROLL_LOCK;
  }
  return leds;
}

size_t espUsbHostBuildKeyboardEvents(uint8_t interfaceNumber,
                                     const EspUsbHostKeyboardReport &previousReport,
                                     const EspUsbHostKeyboardReport &currentReport,
                                     EspUsbHostKeyboardLayout layout,
                                     EspUsbHostKeyboardEvent *events,
                                     size_t maxEvents)
{
  if (!events || maxEvents == 0)
  {
    return 0;
  }

  const uint8_t modifiers = currentReport.data[0];
  const uint8_t *keys = currentReport.data + 2;
  const uint8_t *lastKeys = previousReport.data + 2;
  size_t eventCount = 0;

  for (int i = 0; i < 6 && eventCount < maxEvents; i++)
  {
    const uint8_t key = keys[i];
    if (key == 0)
    {
      continue;
    }

    bool existed = false;
    for (int j = 0; j < 6; j++)
    {
      if (lastKeys[j] == key)
      {
        existed = true;
        break;
      }
    }

    if (!existed)
    {
      EspUsbHostKeyboardEvent &event = events[eventCount++];
      event.interfaceNumber = interfaceNumber;
      event.pressed = true;
      event.released = false;
      event.keycode = key;
      event.ascii = espUsbHostKeycodeToAscii(key, modifiers, layout);
      event.modifiers = modifiers;
    }
  }

  for (int i = 0; i < 6 && eventCount < maxEvents; i++)
  {
    const uint8_t key = lastKeys[i];
    if (key == 0)
    {
      continue;
    }

    bool stillPressed = false;
    for (int j = 0; j < 6; j++)
    {
      if (keys[j] == key)
      {
        stillPressed = true;
        break;
      }
    }

    if (!stillPressed)
    {
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
