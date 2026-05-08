#include "EspUsbHost.h"

EspUsbHost usb;

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
  delay(1);
}
