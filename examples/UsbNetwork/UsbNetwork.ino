#include "EspUsbHost.h"
#include <HTTPClient.h>

// USB host that brings up a USB Ethernet adapter (CDC-NCM / CDC-ECM) as an lwIP
// network interface, so standard Arduino networking (NetworkClient / HTTPClient)
// runs over USB with no Wi-Fi.
//
// Arduino-ESP32 3.3.11 and later can select a non-default USB configuration
// during enumeration. The selector below chooses the CDC-NCM configuration of
// an AX88179A. For other adapters, plug the device in and read the "network
// candidates" report this sketch prints on connect: it lists every CDC-ECM /
// CDC-NCM interface found in *any* configuration, so the configuration value to
// return from the selector can be read straight off the serial log.
//
// Adapters whose network function is not in the default configuration therefore
// need two enumerations: one to discover the value, one to run with it (add the
// printed rule, then reset the board). See README "Why Two Passes Are Needed"
// for the reasons and for how to automate pass 2 with end() / begin().

EspUsbHost usb;

// Set this to a reachable URL when an HTTP connectivity check is wanted.
// "http://192.168.7.1/" is suitable for the EspUsbDevice UsbNetwork peer.
static constexpr const char *HTTP_TEST_URL = "https://httpbin.org/get";
static uint8_t nicAddress = 0;
static uint16_t nicVid = 0;
static uint16_t nicPid = 0;
static uint8_t nicConfiguration = 0;
static uint32_t nicConnectedAt = 0;
static bool candidatesReported = false;
static bool attached = false;
static uint32_t lastPoll = 0;
static bool httpTestDone = false;

// Print every CDC-ECM / CDC-NCM candidate the device exposes, in every
// configuration, together with the configuration that is currently active.
// networkOpen() only accepts a candidate from the active configuration, so this
// is what tells you whether a selector rule is needed and which value to use.
static void reportNetworkCandidates(uint8_t address)
{
  EspUsbHostNetworkInterfaceInfo networks[ESP_USB_HOST_MAX_NETWORK_INTERFACES];
  const size_t count = usb.getNetworkInterfaces(address, networks, ESP_USB_HOST_MAX_NETWORK_INTERFACES);

  Serial.printf("Network candidates: %u (active configuration=%u)\n",
                static_cast<unsigned>(count), nicConfiguration);
  if (count == 0)
  {
    Serial.println("  none - this device does not expose CDC-ECM or CDC-NCM");
    return;
  }

  bool usable = false;
  for (size_t i = 0; i < count; i++)
  {
    espUsbHostPrint(networks[i], Serial);
    if (!networks[i].complete())
    {
      continue;
    }
    if (networks[i].configurationValue == nicConfiguration)
    {
      usable = true;
    }
    else
    {
      Serial.printf("  -> %s lives in configuration %u; add to the selector:\n",
                    espUsbHostNetworkProtocolName(networks[i].protocol),
                    networks[i].configurationValue);
      Serial.printf("       if (device.idVendor == 0x%04x && device.idProduct == 0x%04x) return %u;\n",
                    nicVid, nicPid, networks[i].configurationValue);
    }
  }

  if (usable)
  {
    Serial.println("  -> a usable candidate is in the active configuration; no selector rule needed");
  }
}

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
                          nicVid = device.vid;
                          nicPid = device.pid;
                          nicConfiguration = device.configurationValue;
                          nicConnectedAt = millis();
                          candidatesReported = false;
                          Serial.printf("Device connected: address=%u vid=%04x pid=%04x config=%u\n",
                                        device.address, device.vid, device.pid, device.configurationValue); });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             (void)device;
                             nicAddress = 0;
                             candidatesReported = false;
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
  // Descriptor scanning must not run inside the USB client task, so the report
  // is emitted here rather than from onDeviceConnected().
  if (nicAddress && !candidatesReported && millis() - nicConnectedAt > 500)
  {
    candidatesReported = true;
    reportNetworkCandidates(nicAddress);
  }

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
