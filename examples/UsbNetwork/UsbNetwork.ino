#include "EspUsbHost.h"
#include <HTTPClient.h>

// USB host that brings up a USB Ethernet adapter (CDC-NCM / CDC-ECM) as an lwIP
// network interface, so standard Arduino networking (NetworkClient / HTTPClient)
// runs over USB with no Wi-Fi.
//
// Arduino-ESP32 3.3.11 and later can select a non-default USB configuration
// during enumeration. The selector below chooses the CDC-NCM configuration of
// an AX88179A. Add rules for other adapters after inspecting their descriptors
// with tests/manual/usb_network_descriptor.

EspUsbHost usb;

// Set this to a reachable URL when an HTTP connectivity check is wanted.
// "http://192.168.7.1/" is suitable for the EspUsbDevice UsbNetwork peer.
static constexpr const char *HTTP_TEST_URL = "https://httpbin.org/get";
static uint8_t nicAddress = 0;
static bool attached = false;
static uint32_t lastPoll = 0;
static bool httpTestDone = false;

void setup()
{
  Serial.begin(115200);
  delay(1000);

  if (!usb.setConfigurationSelector([](const usb_device_desc_t &device) -> uint8_t
                                    {
                                      if (device.idVendor == 0x0b95 && device.idProduct == 0x1790)
                                      {
                                        return 2; // AX88179A CDC-NCM configuration
                                      }
                                      return 0; // Keep the device's default configuration
                                    }))
  {
    Serial.printf("USB configuration selector unavailable: %s\n", usb.lastErrorName());
  }

  // Record the device on connect; opening the interface and attaching the netif
  // must happen outside the USB client task, so we do it from loop().
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          nicAddress = device.address;
                          Serial.printf("Device connected: address=%u vid=%04x pid=%04x\n",
                                        device.address, device.vid, device.pid); });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             (void)device;
                             nicAddress = 0;
                             attached = false;
                             httpTestDone = false;
                             Serial.println("Device disconnected"); });

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

      if (HTTP_TEST_URL && !httpTestDone)
      {
        httpTestDone = true;
        HTTPClient http;
        if (http.begin(HTTP_TEST_URL))
        {
          int code = http.GET();
          Serial.printf("HTTP GET %s -> %d\n", HTTP_TEST_URL, code);
          if (code == 200)
          {
            Serial.println(http.getString());
          }
          http.end();
        }
      }
    }
    else
    {
      Serial.println("Waiting for DHCP lease over USB...");
    }
  }

  delay(1);
}
