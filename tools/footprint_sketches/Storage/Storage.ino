#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostMscFS storage;

void setup()
{
  usb.begin();
  storage.begin(usb, "/usb");
}

void loop()
{
  if (storage.mounted())
  {
    storage.exists("/");
  }
  delay(1);
}
