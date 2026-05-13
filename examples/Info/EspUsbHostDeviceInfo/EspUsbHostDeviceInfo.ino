#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

static const char *speedName(usb_speed_t speed)
{
  switch (speed)
  {
  case USB_SPEED_LOW:
    return "low-speed";
  case USB_SPEED_FULL:
    return "full-speed";
  case USB_SPEED_HIGH:
    return "high-speed";
  default:
    return "unknown";
  }
}

static const char *className(uint8_t cls)
{
  switch (cls)
  {
  case 0x00:
    return "per-interface";
  case 0x01:
    return "Audio";
  case 0x02:
    return "CDC Control";
  case 0x03:
    return "HID";
  case 0x08:
    return "Mass Storage";
  case 0x09:
    return "Hub";
  case 0x0a:
    return "CDC Data";
  case 0x0e:
    return "Video";
  case 0xff:
    return "Vendor";
  default:
    return "Unknown";
  }
}

static const char *transferTypeName(uint8_t attributes)
{
  switch (attributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK)
  {
  case USB_BM_ATTRIBUTES_XFER_CONTROL:
    return "control";
  case USB_BM_ATTRIBUTES_XFER_ISOC:
    return "isochronous";
  case USB_BM_ATTRIBUTES_XFER_BULK:
    return "bulk";
  case USB_BM_ATTRIBUTES_XFER_INT:
    return "interrupt";
  default:
    return "unknown";
  }
}

static void printDevice(uint8_t address)
{
  EspUsbHostDeviceInfo device;
  if (!usb.getDevice(address, device))
  {
    Serial.printf("Device address=%u not found\n", address);
    return;
  }

  Serial.println();
  Serial.println("=========== USB Device ===========");
  Serial.printf("Address %u", device.address);
  if (device.parentAddress)
  {
    Serial.printf(" parent=%u port=%u", device.parentAddress, device.parentPort);
  }
  else
  {
    Serial.print(" parent=root");
  }
  Serial.printf(" speed=%s\n", speedName(device.speed));
  Serial.printf("VID:PID %04x:%04x class=0x%02x(%s) subclass=0x%02x protocol=0x%02x\n",
                device.vid,
                device.pid,
                device.deviceClass,
                className(device.deviceClass),
                device.deviceSubClass,
                device.deviceProtocol);
  Serial.printf("USB %x.%02x device %x.%02x ep0=%u\n",
                device.usbVersion >> 8,
                device.usbVersion & 0xff,
                device.deviceVersion >> 8,
                device.deviceVersion & 0xff,
                device.maxPacketSize0);
  Serial.printf("Strings manufacturer=\"%s\" product=\"%s\" serial=\"%s\"\n",
                device.manufacturer,
                device.product,
                device.serial);
  Serial.printf("Configuration value=%u interfaces=%u total_len=%u attributes=0x%02x max_power=%umA\n",
                device.configurationValue,
                device.configurationInterfaceCount,
                device.configurationTotalLength,
                device.configurationAttributes,
                device.configurationMaxPower * 2);

  EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
  const size_t interfaceCount = usb.getInterfaces(address, interfaces, ESP_USB_HOST_MAX_INTERFACES);
  for (size_t i = 0; i < interfaceCount; i++)
  {
    const EspUsbHostInterfaceInfo &intf = interfaces[i];
    Serial.printf("  Interface %u alt=%u class=0x%02x(%s) subclass=0x%02x protocol=0x%02x endpoints=%u\n",
                  intf.number,
                  intf.alternate,
                  intf.interfaceClass,
                  className(intf.interfaceClass),
                  intf.interfaceSubClass,
                  intf.interfaceProtocol,
                  intf.endpointCount);
  }

  EspUsbHostEndpointInfo endpoints[ESP_USB_HOST_MAX_ENDPOINTS];
  const size_t endpointCount = usb.getEndpoints(address, endpoints, ESP_USB_HOST_MAX_ENDPOINTS);
  for (size_t i = 0; i < endpointCount; i++)
  {
    const EspUsbHostEndpointInfo &ep = endpoints[i];
    Serial.printf("    Endpoint iface=%u ep=0x%02x dir=%s type=%s max_packet=%u interval=%u attrs=0x%02x\n",
                  ep.interfaceNumber,
                  ep.address,
                  (ep.address & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) ? "IN" : "OUT",
                  transferTypeName(ep.attributes),
                  ep.maxPacketSize,
                  ep.interval,
                  ep.attributes);
  }
  Serial.println("========= USB Device End =========");
  Serial.println();
}

static void printAllDevices()
{
  EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
  const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
  if (count == 0)
  {
    Serial.println("No USB devices");
    return;
  }
  for (size_t i = 0; i < count; i++)
  {
    printDevice(devices[i].address);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("EspUsbHostDeviceInfo start");
  Serial.println("Press 'r' to reprint connected devices.");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.printf("CONNECTED address=%u\n", device.address);
                          printDevice(device.address);
                        });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             Serial.printf("DISCONNECTED address=%u vid=%04x pid=%04x\n",
                                           device.address,
                                           device.vid,
                                           device.pid);
                           });

  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (Serial.available() > 0 && Serial.read() == 'r')
  {
    printAllDevices();
  }
  delay(10);
}
