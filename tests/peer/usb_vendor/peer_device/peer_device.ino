#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceVendor Vendor(device);

static volatile uint32_t rxCount = 0;
static volatile uint32_t controlCount = 0;

void setup()
{
  Serial.begin(115200);
  delay(500);

  Vendor.onRx([](size_t available)
              {
                uint8_t buffer[64];
                while (available > 0)
                {
                  const size_t chunk = Vendor.read(buffer, min(available, sizeof(buffer)));
                  if (chunk == 0)
                  {
                    break;
                  }
                  rxCount += chunk;
                  Vendor.write(reinterpret_cast<const uint8_t *>("echo:"), 5);
                  Vendor.write(buffer, chunk);
                  Vendor.flush();
                  available = Vendor.available();
                }
              });

  Vendor.onControlRequest([](const EspUsbDeviceVendorControlRequest &request)
                          {
                            controlCount++;
                            static const char info[] = "EspUsbDeviceVendor";
                            if ((request.bmRequestType & 0x80) && request.bRequest == 0x01)
                            {
                              return Vendor.sendControlResponse(request, info, min(static_cast<size_t>(request.wLength), sizeof(info) - 1));
                            }
                            if (!(request.bmRequestType & 0x80) && request.bRequest == 0x02)
                            {
                              return Vendor.sendControlResponse(request);
                            }
                            return false;
                          });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4019;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice USB Vendor";
  config.serialNumber = "espusb-usb-vendor";
  config.webusbEnabled = true;
  config.webusbUrl = "example.com/espusbdevice";
  Serial.printf("DEVICE_BEGIN %u\n", device.begin(config) ? 1 : 0);
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.println("DEVICE_READY");
    }
    else if (command == 's')
    {
      Serial.printf("DEVICE_STATUS rx=%lu control=%lu\n",
                    static_cast<unsigned long>(rxCount),
                    static_cast<unsigned long>(controlCount));
    }
  }
  delay(1);
}
