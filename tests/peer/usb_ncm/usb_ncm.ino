#include "EspUsbHost.h"
#include <HTTPClient.h>

// Host side of the CDC-NCM peer test. Pairs with the EspUsbDevice NCM device in
// peer_device/, which runs a DHCP server at 192.168.7.1 and serves a fixed body
// at "/". The host enumerates the NCM interface, attaches it as a DHCP-client
// netif, obtains a 192.168.7.x lease, and does an HTTP GET over the USB link.

EspUsbHost usb;

static volatile bool connected = false;
static uint8_t deviceAddress = 0;
static bool attached = false;
static uint32_t lastReportedIp = 0;

static void reportEnumeration()
{
  EspUsbHostNetworkInterfaceInfo networks[ESP_USB_HOST_MAX_NETWORK_INTERFACES];
  const size_t count = usb.getNetworkInterfaces(deviceAddress, networks, ESP_USB_HOST_MAX_NETWORK_INTERFACES);
  EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
  const size_t interfaceCount = usb.getInterfaces(deviceAddress, interfaces, ESP_USB_HOST_MAX_INTERFACES);
  size_t claimAttempts = 0;
  size_t claimed = 0;
  for (size_t i = 0; i < interfaceCount; i++)
  {
    claimAttempts += interfaces[i].claimAttempted ? 1 : 0;
    claimed += interfaces[i].claimed ? 1 : 0;
  }
  int selected = -1;
  for (size_t i = 0; i < count; i++)
  {
    if (networks[i].complete())
    {
      selected = static_cast<int>(i);
      if (networks[i].protocol == ESP_USB_HOST_NETWORK_PROTOCOL_CDC_NCM)
      {
        break;
      }
    }
  }
  if (selected < 0)
  {
    Serial.printf("NCM_ENUM count=%u protocol=none complete=0\n", static_cast<unsigned>(count));
    return;
  }
  Serial.printf("NCM_ENUM count=%u protocol=%s complete=1 ctrl=%u data=%u alt=%u in=0x%02x out=0x%02x notify=0x%02x claim_attempts=%u claimed=%u managed=%u error=%s\n",
                static_cast<unsigned>(count),
                espUsbHostNetworkProtocolName(networks[selected].protocol),
                networks[selected].controlInterfaceNumber,
                networks[selected].dataInterfaceNumber,
                networks[selected].dataInterfaceAlternate,
                networks[selected].inEndpoint,
                networks[selected].outEndpoint,
                networks[selected].notificationEndpoint,
                static_cast<unsigned>(claimAttempts),
                static_cast<unsigned>(claimed),
                static_cast<unsigned>(usb.managedEndpointCount(deviceAddress)),
                usb.lastErrorName());
}

void setup()
{
  // Event prints can burst faster than the default serial TX buffer
  // drains; enlarge it so lines are not truncated mid-flight.
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          deviceAddress = device.address;
                          connected = true;
                          Serial.printf("HOST_CONNECTED address=%u vid=%04x pid=%04x\n",
                                        device.address, device.vid, device.pid);
                        });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             (void)device;
                             connected = false;
                             attached = false;
                             lastReportedIp = 0;
                           });

  if (!usb.begin())
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
  }
}

void loop()
{
  // Auto-report the DHCP lease once it arrives so the test can wait for it.
  if (attached)
  {
    const uint32_t ip = static_cast<uint32_t>(usb.networkLocalIP(deviceAddress));
    if (ip != 0 && ip != lastReportedIp)
    {
      lastReportedIp = ip;
      Serial.printf("NETWORK_IP ip=%s\n", IPAddress(ip).toString().c_str());
    }
  }

  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'i')
    {
      if (connected)
      {
        reportEnumeration();
      }
      else
      {
        Serial.println("NCM_ENUM count=0 protocol=none complete=0");
      }
    }
    else if (command == 'a')
    {
      EspUsbHostNetworkConfig cfg; // dhcpClient = true
      attached = usb.networkAttachNetif(cfg, deviceAddress);
      Serial.printf("NETWORK_ATTACH ok=%u\n", attached ? 1 : 0);
    }
    else if (command == 'p')
    {
      const uint32_t ip = static_cast<uint32_t>(usb.networkLocalIP(deviceAddress));
      Serial.printf("NETWORK_IP ip=%s\n", IPAddress(ip).toString().c_str());
    }
    else if (command == 'd')
    {
      EspUsbHostNetworkStats st;
      usb.networkStats(st, deviceAddress);
      Serial.printf("NETWORK_STATS ready=%u link=%u netif=%u rxNtb=%lu rxFrames=%lu tx=%lu txFail=%lu ip=%s\n",
                    st.ready ? 1 : 0, st.linkUp ? 1 : 0, st.netifAttached ? 1 : 0,
                    static_cast<unsigned long>(st.rxNtb),
                    static_cast<unsigned long>(st.rxFrames),
                    static_cast<unsigned long>(st.txFrames),
                    static_cast<unsigned long>(st.txFails),
                    usb.networkLocalIP(deviceAddress).toString().c_str());
    }
    else if (command == 'g')
    {
      HTTPClient http;
      int code = -1;
      String body;
      if (http.begin("http://192.168.7.1/"))
      {
        code = http.GET();
        if (code == 200)
        {
          body = http.getString();
        }
        http.end();
      }
      body.trim();
      Serial.printf("HTTP_GET code=%d body=%s\n", code, body.c_str());
    }
  }
  delay(1);
}
