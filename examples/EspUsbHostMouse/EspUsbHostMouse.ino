#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onMouse([](const EspUsbHostMouseEvent &event)
              {
    if (event.buttonsChanged) {
      Serial.printf("buttons: 0x%02x -> 0x%02x\n",
                    event.previousButtons,
                    event.buttons);
    }
    if (event.moved) {
      Serial.printf("move: x=%d y=%d wheel=%d buttons=0x%02x\n",
                    event.x,
                    event.y,
                    event.wheel,
                    event.buttons);
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
