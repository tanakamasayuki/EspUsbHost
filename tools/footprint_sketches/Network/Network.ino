#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  usb.begin();
}

void loop()
{
  EspUsbHostNetworkConfig config;
  usb.networkAttachNetif(config);
  usb.networkLocalIP();
  delay(1000);
}
