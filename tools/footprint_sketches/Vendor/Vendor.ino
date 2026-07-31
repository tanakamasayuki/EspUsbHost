#include "EspUsbHost.h"
#include "espusbhost_version.h"

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

#if ESPUSBHOST_VERSION_MAJOR > 2 || \
    (ESPUSBHOST_VERSION_MAJOR == 2 && ESPUSBHOST_VERSION_MINOR > 5) || \
    (ESPUSBHOST_VERSION_MAJOR == 2 && ESPUSBHOST_VERSION_MINOR == 5 && ESPUSBHOST_VERSION_PATCH >= 3)
    // Reference the asynchronous bulk OUT queue and ZLP handling too, so this
    // probe keeps covering the whole vendor bulk surface.
    usb.vendorSetAutoZlp(true);
    if (usb.vendorWriteQueueBegin(2, 1024))
    {
      size_t capacity = 0;
      uint8_t *buffer = usb.vendorWriteAcquire(&capacity, 10);
      if (buffer)
      {
        buffer[0] = 0;
        usb.vendorWriteSubmit(buffer, 1);
      }
      usb.vendorWriteAsync(payload, sizeof(payload));
      usb.vendorWriteZlp();
      usb.vendorWriteFlush(100);
      const EspUsbHostVendorWriteStats stats = usb.vendorWriteStats();
      (void)stats.bytes;
      (void)usb.vendorWritePending();
      (void)usb.vendorWriteQueueFree();
      (void)usb.vendorOutPacketSize();
      (void)usb.vendorOutEndpoint();
      usb.vendorWriteQueueEnd();
    }
#endif
  }
  delay(1000);
}
