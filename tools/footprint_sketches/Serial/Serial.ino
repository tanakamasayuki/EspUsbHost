#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostCdcSerial cdc(usb);

void setup()
{
  cdc.begin(115200);
  usb.begin();
}

void loop()
{
  if (cdc.available())
  {
    cdc.write(cdc.read());
  }
  delay(1);
}
