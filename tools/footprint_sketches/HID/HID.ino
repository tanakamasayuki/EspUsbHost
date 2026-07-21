#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 { (void)event; });
  usb.begin();
}

void loop()
{
  delay(1);
}
