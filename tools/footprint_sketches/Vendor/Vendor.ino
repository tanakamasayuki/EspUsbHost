#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  usb.onVendorData([](const EspUsbHostVendorData &data)
                   { (void)data; });
  usb.begin();
}

void loop()
{
  static const uint8_t payload[] = {0};
  if (usb.vendorOpen())
  {
    usb.vendorWrite(payload, sizeof(payload));
  }
  delay(1000);
}
