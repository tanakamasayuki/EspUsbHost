#include "EspUsbHost.h"

EspUsbHost usb;

static void printModifiers(uint8_t modifiers)
{
  static const char *modifierNames[] = {
      "LCTRL", "LSHIFT", "LALT", "LGUI",
      "RCTRL", "RSHIFT", "RALT", "RGUI"};

  if (modifiers == 0)
  {
    Serial.print("none");
    return;
  }

  bool first = true;
  for (int i = 0; i < 8; i++)
  {
    if (modifiers & (1 << i))
    {
      if (!first)
      {
        Serial.print('+');
      }
      Serial.print(modifierNames[i]);
      first = false;
    }
  }
}

static void onKeyboard(const EspUsbHostKeyboardEvent &event)
{
  const char displayChar = (event.ascii >= 0x20 && event.ascii != 0x7F) ? static_cast<char>(event.ascii) : '.';

  Serial.printf("[%s] keycode=0x%02x ascii=0x%02x(%c) modifiers=",
                event.pressed ? "press  " : "release",
                event.keycode,
                event.ascii,
                displayChar);
  printModifiers(event.modifiers);
  Serial.println();
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP);

  usb.onDeviceConnected(espUsbHostPrintDeviceConnected);
  usb.onDeviceDisconnected(espUsbHostPrintDeviceDisconnected);
  usb.onKeyboard(onKeyboard);

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(1);
}
