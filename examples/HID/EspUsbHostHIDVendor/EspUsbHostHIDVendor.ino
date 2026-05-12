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

  Serial.println("EspUsbHost HID vendor example start");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        { Serial.printf("connected: vid=%04x pid=%04x product=%s\n", info.vid, info.pid, info.product); });

  usb.onVendorInput([](const EspUsbHostVendorInput &input)
                    {
    Serial.printf("vendor iface=%u len=%u data=", input.interfaceNumber, (unsigned)input.length);
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
  if (Serial.available() > 0)
  {
    char command = Serial.read();
    if (command == 'o')
    {
      Serial.printf("send output: %s\n", usb.sendVendorOutput(OUTPUT_REPORT, sizeof(OUTPUT_REPORT)) ? "ok" : "failed");
    }
    else if (command == 'f')
    {
      Serial.printf("send feature: %s\n", usb.sendVendorFeature(FEATURE_REPORT, sizeof(FEATURE_REPORT)) ? "ok" : "failed");
    }
  }
  delay(1);
}
