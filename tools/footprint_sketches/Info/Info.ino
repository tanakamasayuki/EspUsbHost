#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  usb.begin();
}

void loop()
{
  usb.printAllDeviceInfo();
  delay(1000);
}
