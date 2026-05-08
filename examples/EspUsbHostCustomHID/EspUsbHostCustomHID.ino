#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println("EspUsbHost custom HID example start");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        { Serial.printf("connected: vid=%04x pid=%04x product=%s\n", info.vid, info.pid, info.product); });

  usb.onHIDInput([](const EspUsbHostHIDInput &input)
                 {
    Serial.printf("hid iface=%u subclass=%u protocol=%u len=%u data=",
                  input.interfaceNumber,
                  input.subclass,
                  input.protocol,
                  (unsigned)input.length);
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
