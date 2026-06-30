#include "EspUsbHost.h"

EspUsbHost usb;

static const uint8_t OUTPUT_REPORT[63] = {
    'h', 'o', 's', 't', ' ', 'o', 'u', 't', 'p', 'u', 't'};
static const uint8_t FEATURE_REPORT[63] = {
    'h', 'o', 's', 't', ' ', 'f', 'e', 'a', 't', 'u', 'r', 'e'};

void setup()
{
    Serial.begin(115200);
    delay(500);

    usb.onHIDVendorInput([](const EspUsbHostHIDVendorInput &input)
                      {
      Serial.print("VENDOR ");
      for (size_t i = 0; i < input.reportLength && input.reportData[i] != 0; i++)
      {
          Serial.write(input.reportData[i]);
      }
      Serial.println(); });

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
        if (command == 'o')
        {
            Serial.printf("SEND_OUTPUT %u\n", usb.sendHIDVendorOutput(OUTPUT_REPORT, sizeof(OUTPUT_REPORT)) ? 1 : 0);
        }
        else if (command == 'f')
        {
            Serial.printf("SEND_FEATURE %u\n", usb.sendHIDVendorFeature(FEATURE_REPORT, sizeof(FEATURE_REPORT)) ? 1 : 0);
        }
    }
    delay(1);
}
