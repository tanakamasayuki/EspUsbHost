#include "EspUsbDevice.h"
#include <WebServer.h>

// Peer device for the CDC-NCM host test. Brings up a USB network device
// (CDC-NCM) with a DHCP server at 192.168.7.1 and serves a fixed body at "/".
// The host (usb_ncm.ino) gets a 192.168.7.x lease and fetches this page.

EspUsbDevice device;
EspUsbDeviceNet net(device);
WebServer server(80);

static const char kBody[] = "ESPUSB_NCM_OK";

void setup()
{
  Serial.begin(115200);
  delay(500);

  net.ipConfig(IPAddress(192, 168, 7, 1), IPAddress(192, 168, 7, 1), IPAddress(255, 255, 255, 0));
  net.dhcpServer(true);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4032;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice NCM";
  config.serialNumber = "espusb-ncm-peer";

  if (!device.begin(config))
  {
    Serial.printf("DEVICE_BEGIN 0 %s\n", device.lastErrorName());
    return;
  }
  if (!net.beginNetwork())
  {
    Serial.println("NET_BEGIN 0");
    return;
  }

  server.on("/", []()
            { server.send(200, "text/plain", kBody); });
  server.begin();

  Serial.printf("DEVICE_BEGIN 1 ip=%s\n", net.localIP().toString().c_str());
}

void loop()
{
  server.handleClient();

  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.printf("DEVICE_READY ip=%s link=%u\n",
                    net.localIP().toString().c_str(),
                    net.linkUp() ? 1 : 0);
    }
  }
  delay(1);
}
