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
          Serial.printf("KEY %c\n", (char)event.ascii);
      } });

    usb.onMouse([](const EspUsbHostMouseEvent &event)
                {
      if (event.moved || event.buttonsChanged)
      {
          Serial.printf("MOUSE x=%d y=%d wheel=%d buttons=%u previous=%u moved=%u changed=%u\n",
                        event.x,
                        event.y,
                        event.wheel,
                        event.buttons,
                        event.previousButtons,
                        event.moved ? 1 : 0,
                        event.buttonsChanged ? 1 : 0);
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
