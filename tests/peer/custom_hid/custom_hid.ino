#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
    Serial.begin(115200);
    delay(500);

    usb.onHIDInput([](const EspUsbHostHIDInput &input)
                   {
      Serial.printf("CUSTOM len=%u data=", (unsigned)input.length);
      for (size_t i = 0; i < input.length; i++)
      {
          Serial.printf("%02x", input.data[i]);
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
