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
                      Serial.printf(" fields=%u", (unsigned)event.fieldCount);
                      for (size_t i = 0; i < event.fieldCount; i++)
                      {
                          const EspUsbHostHIDFieldValue &field = event.fields[i];
                          Serial.printf(" %04x:%04x=%ld",
                                        field.usagePage,
                                        field.usage,
                                        (long)field.value);
                      }
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
