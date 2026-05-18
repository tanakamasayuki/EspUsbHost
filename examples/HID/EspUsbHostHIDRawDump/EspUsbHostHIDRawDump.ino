#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected(espUsbHostPrintDeviceConnected);
  usb.onDeviceDisconnected(espUsbHostPrintDeviceDisconnected);
  usb.onHIDInput(espUsbHostPrintHIDInput);

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
}
