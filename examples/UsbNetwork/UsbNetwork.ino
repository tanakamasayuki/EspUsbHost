#include "EspUsbHost.h"
#include <HTTPClient.h>

// USB host that brings up a USB Ethernet adapter (CDC-NCM / CDC-ECM) as an lwIP
// network interface, so standard Arduino networking (NetworkClient / HTTPClient)
// runs over USB with no Wi-Fi.
//
// EXPERIMENTAL — NOT usable with real USB NICs yet. Real adapters expose their
// CDC-NCM/ECM interface in a NON-active USB configuration, and selecting a
// non-active configuration is not possible on the current Arduino-ESP32 core
// (CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK is off). That is planned for
// after the next Arduino-ESP32 release. For now this sketch only works against a
// second ESP32-S3 board running the sibling EspUsbDevice "UsbNetwork" sketch,
// which presents CDC-NCM as its active configuration (a DHCP server at
// 192.168.7.1); this host gets a 192.168.7.x lease and reaches it.

EspUsbHost usb;

static uint8_t nicAddress = 0;
static bool attached = false;
static uint32_t lastPoll = 0;

void setup()
{
  Serial.begin(115200);
  delay(1000);

  // Record the device on connect; opening the interface and attaching the netif
  // must happen outside the USB client task, so we do it from loop().
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          nicAddress = device.address;
                          Serial.printf("Device connected: address=%u vid=%04x pid=%04x\n",
                                        device.address, device.vid, device.pid);
                        });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             (void)device;
                             nicAddress = 0;
                             attached = false;
                             Serial.println("Device disconnected");
                           });

  if (!usb.begin())
  {
    Serial.printf("USB host begin failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  // Once a USB network adapter is enumerated, attach it as a DHCP-client netif.
  if (nicAddress && !attached)
  {
    EspUsbHostNetworkConfig cfg; // dhcpClient = true by default
    if (usb.networkAttachNetif(cfg, nicAddress))
    {
      attached = true;
      Serial.println("USB network interface attached (waiting for DHCP lease)");
    }
    else
    {
      delay(500); // not a network device yet, or open failed; retry
    }
  }

  // Report the lease and demonstrate HTTP over the USB link once we have an IP.
  if (attached && millis() - lastPoll > 3000)
  {
    lastPoll = millis();
    IPAddress ip = usb.networkLocalIP(nicAddress);
    if (static_cast<uint32_t>(ip) != 0)
    {
      Serial.printf("USB NIC IP: %s\n", ip.toString().c_str());

      // The paired EspUsbDevice serves a page at its gateway address.
      HTTPClient http;
      if (http.begin("http://192.168.7.1/"))
      {
        int code = http.GET();
        Serial.printf("HTTP GET http://192.168.7.1/ -> %d\n", code);
        if (code == 200)
        {
          Serial.println(http.getString());
        }
        http.end();
      }
    }
    else
    {
      Serial.println("Waiting for DHCP lease over USB...");
    }
  }

  delay(1);
}
