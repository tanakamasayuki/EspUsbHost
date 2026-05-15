#include "EspUsbHost.h"

EspUsbHost usb;

static const char *modifierNames[] = {
    "LCTRL", "LSHIFT", "LALT", "LGUI",
    "RCTRL", "RSHIFT", "RALT", "RGUI"};

static void printModifiers(uint8_t modifiers)
{
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
        Serial.print('+');
      Serial.print(modifierNames[i]);
      first = false;
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        { Serial.printf("connected: vid=%04x pid=%04x product=%s\n",
                                        device.vid, device.pid, device.product); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &)
                           { Serial.println("disconnected"); });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
    char displayChar = (event.ascii >= 0x20 && event.ascii != 0x7F) ? (char)event.ascii : '.';
    Serial.printf("[%s] keycode=0x%02x ascii=0x%02x(%c) modifiers=",
                  event.pressed ? "press  " : "release",
                  event.keycode,
                  event.ascii,
                  displayChar);
    printModifiers(event.modifiers);
    Serial.println(); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(1);
}
