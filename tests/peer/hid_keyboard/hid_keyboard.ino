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
    if (Serial.available() > 0)
    {
        char command = Serial.read();
        if (command == 'n')
        {
            Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(true, false, false) ? 1 : 0);
        }
        else if (command == 'c')
        {
            Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, true, false) ? 1 : 0);
        }
        else if (command == 's')
        {
            Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, false, true) ? 1 : 0);
        }
        else if (command == '0')
        {
            Serial.printf("LED_TX %u\n", usb.setKeyboardLeds(false, false, false) ? 1 : 0);
        }
    }
    delay(1);
}
