#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
    Serial.begin(115200);
    delay(500);

    usb.onVendorInput([](const EspUsbHostVendorInput &input)
                      {
      Serial.print("VENDOR ");
      for (size_t i = 0; i < input.length && input.data[i] != 0; i++)
      {
          Serial.write(input.data[i]);
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
