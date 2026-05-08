#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
    Serial.begin(115200);
    delay(500);

    usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                   {
      if (event.pressed && event.ascii)
      {
          Serial.print((char)event.ascii);
      } });

    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }
}

void loop()
{
    delay(1);
}
