#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceVendor Vendor(device);

static volatile uint32_t rxCount = 0;
static volatile uint32_t controlCount = 0;

// Built here rather than inline in setup() so the detach/attach commands can
// bring the device back up with exactly the same descriptors.
static bool startDevice()
{
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4019;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice USB Vendor";
  config.serialNumber = "espusb-usb-vendor";
  config.webusbEnabled = true;
  config.webusbUrl = "https://example.com/espusbdevice";
  return device.begin(config);
}

static void processVendorRx()
{
  size_t available = Vendor.available();
  uint8_t buffer[64];
  while (available > 0)
  {
    const size_t chunk = Vendor.read(buffer, min(available, sizeof(buffer)));
    if (chunk == 0)
    {
      break;
    }
    rxCount += chunk;
    // One write, so the reply leaves as one transfer: with autoFlush (3.x's
    // default) a separate "echo:" write would go out on its own.
    uint8_t reply[5 + 64];
    const size_t replyLength = 5 + (chunk < 64 ? chunk : 64);
    memcpy(reply, "echo:", 5);
    memcpy(reply + 5, buffer, replyLength - 5);
    Vendor.write(reply, replyLength);
    Vendor.flush();
    available = Vendor.available();
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  // Received data is read from loop(): onRx() runs on the USB worker, which
  // must not block on the transmit FIFO that the echo writes into. 2.x ended a
  // transfer whose length is a multiple of the packet size with a zero-length
  // packet; 3.x does not unless asked, so ask.
  EspUsbVendorConfig vendorConfig = Vendor.configuration();
  vendorConfig.streamTermination = EspUsbStreamTermination::EndWithShortPacket;
  if (Vendor.configure(vendorConfig) != ESP_OK)
  {
    Serial.println("DEVICE_CONFIGURE_FAILED");
  }

  // Called once per control stage. Both requests are answered at SETUP; the
  // later stages only acknowledge (0x11 has no data stage).
  Vendor.onControlRequest([](const EspUsbDeviceVendorControlRequest &request)
                          {
                            if (request.stage != ESP_USB_DEVICE_CONTROL_STAGE_SETUP)
                            {
                              return true;
                            }
                            controlCount++;
                            static const char info[] = "EspUsbDeviceVendor";
                            if ((request.bmRequestType & 0x80) && request.bRequest == 0x10)
                            {
                              return Vendor.sendControlResponse(request, info, min(static_cast<size_t>(request.wLength), sizeof(info) - 1));
                            }
                            if (!(request.bmRequestType & 0x80) && request.bRequest == 0x11)
                            {
                              return Vendor.sendControlResponse(request);
                            }
                            return false;
                          });

  Serial.printf("DEVICE_BEGIN %u\n", startDevice() ? 1 : 0);
}

void loop()
{
  processVendorRx();
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.println("DEVICE_READY");
    }
    else if (command == 'X')
    {
      // The host sees an ordinary unplug; 'Y' is the plug back in. Together they
      // let a host-side test cycle a device without anyone touching the cable.
      device.end();
      Serial.println("DEVICE_DETACHED");
    }
    else if (command == 'Y')
    {
      Serial.printf("DEVICE_ATTACHED %u\n", startDevice() ? 1 : 0);
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
