#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
    Serial.begin(115200);
    delay(500);

    usb.onGamepad([](const EspUsbHostGamepadEvent &event)
                  {
                      Serial.printf("GAMEPAD report=");
                      for (size_t i = 0; i < event.reportLength; i++)
                      {
                          Serial.printf("%02x", event.reportData[i]);
                          if (i + 1 < event.reportLength)
                          {
                              Serial.print(" ");
                          }
                      }
                      Serial.printf(" hat=%s%u buttons=0x%08lx previous=0x%08lx\n",
                                    event.hasHat ? "" : "-",
                                    event.hat,
                                    static_cast<unsigned long>(event.buttons),
                                    static_cast<unsigned long>(event.previousButtons)); });

    if (!usb.begin())
    {
        Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
    }
}

void loop()
{
    delay(1);
}
