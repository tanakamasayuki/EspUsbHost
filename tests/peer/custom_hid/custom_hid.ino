#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
    Serial.begin(115200);
    delay(500);

    usb.onHIDReportDescriptor([](const EspUsbHostHIDReportDescriptor &descriptor)
                              { Serial.printf("HID_REPORT iface=%u reported=%u len=%u first=%02x last=%02x\n",
                                              descriptor.interfaceNumber,
                                              descriptor.reportedLength,
                                              descriptor.length,
                                              descriptor.length > 0 ? descriptor.data[0] : 0,
                                              descriptor.length > 0 ? descriptor.data[descriptor.length - 1] : 0); });

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
