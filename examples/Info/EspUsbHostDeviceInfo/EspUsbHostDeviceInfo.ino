#include <Arduino.h>
#include <EspUsbHost.h>

EspUsbHost usb;

static bool printedInitialTopology = false;
static uint32_t lastDeviceEventMs = 0;

static const char *yesNo(bool value)
{
  return value ? "yes" : "no";
}

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

static const char *configAttributeName(uint8_t attributes)
{
  if (attributes & 0x40)
  {
    return "self-powered";
  }
  return "bus-powered";
}

static void printHexBytes(const uint8_t *data, size_t length)
{
  for (size_t i = 0; i < length; i++)
  {
    Serial.printf("%02x", data[i]);
    if (i + 1 < length)
    {
      Serial.print(" ");
    }
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

static void printHubPortStatus(uint8_t hubAddress, uint8_t port)
{
  uint16_t status = 0;
  uint16_t change = 0;
  if (!usb.getHubPortStatus(hubAddress, port, status, change))
  {
    Serial.printf("  Port %u status unavailable\n", port);
    return;
  }

  Serial.printf("  Port %u status=0x%04x change=0x%04x connected=%s enabled=%s suspended=%s over_current=%s reset=%s powered=%s low_speed=%s high_speed=%s test=%s indicator=%s\n",
                port,
                status,
                change,
                yesNo(status & 0x0001),
                yesNo(status & 0x0002),
                yesNo(status & 0x0004),
                yesNo(status & 0x0008),
                yesNo(status & 0x0010),
                yesNo(status & 0x0100),
                yesNo(status & 0x0200),
                yesNo(status & 0x0400),
                yesNo(status & 0x0800),
                yesNo(status & 0x1000));
}

static bool getHubPortConnected(uint8_t hubAddress, uint8_t port, bool &connected)
{
  uint16_t status = 0;
  uint16_t change = 0;
  if (!usb.getHubPortStatus(hubAddress, port, status, change))
  {
    return false;
  }
  connected = (status & 0x0001) != 0;
  return true;
}

static void printHubInfo(uint8_t hubAddress, bool printPorts)
{
  EspUsbHostHubInfo hub;
  if (!usb.getHubInfo(hubAddress, hub))
  {
    Serial.printf("Hub address=%u descriptor unavailable\n", hubAddress);
    return;
  }

  Serial.println("----------- USB Hub -----------");
  Serial.printf("Hub address=%u ports=%u descriptor_len=%u characteristics=0x%04x\n",
                hub.address,
                hub.portCount,
                hub.descriptorLength,
                hub.characteristics);
  Serial.printf("Power switching: per-port=%s ganged=%s none=%s\n",
                yesNo(hub.perPortPowerSwitching),
                yesNo(hub.gangedPowerSwitching),
                yesNo(hub.noPowerSwitching));
  Serial.printf("Over-current: per-port=%s ganged=%s none=%s\n",
                yesNo(hub.perPortOverCurrent),
                yesNo(hub.gangedOverCurrent),
                yesNo(hub.noOverCurrent));
  Serial.printf("Compound=%s power_on_to_good=%ums controller_current=%umA\n",
                yesNo(hub.compound),
                hub.powerOnToPowerGoodMs,
                hub.controllerCurrentMa);
  Serial.print("Raw hub descriptor: ");
  printHexBytes(hub.rawDescriptor, hub.descriptorLength);
  Serial.println();

  if (printPorts)
  {
    for (uint8_t port = 1; port <= hub.portCount; port++)
    {
      printHubPortStatus(hub.address, port);
    }
  }
  Serial.println("--------- USB Hub End ---------");
}

static void printFlattenedHubNotes(const EspUsbHostHubInfo &hub,
                                   const EspUsbHostDeviceInfo *devices,
                                   size_t deviceCount)
{
  if (hub.portCount == 0)
  {
    return;
  }

  bool rootPortHasDevice[16] = {};
  for (size_t i = 0; i < deviceCount; i++)
  {
    const uint8_t upstreamPort = devices[i].parentAddress ? (devices[i].portId & 0x0f) : devices[i].portId;
    if ((devices[i].parentAddress == hub.address || (hub.address == 1 && devices[i].parentAddress == 0)) &&
        upstreamPort > 0 &&
        upstreamPort < sizeof(rootPortHasDevice))
    {
      rootPortHasDevice[upstreamPort] = true;
    }
  }

  bool printedHeader = false;
  for (uint8_t port = 1; port <= hub.portCount && port < sizeof(rootPortHasDevice); port++)
  {
    bool connected = false;
    if (!getHubPortConnected(hub.address, port, connected))
    {
      continue;
    }
    if (connected && !rootPortHasDevice[port])
    {
      if (!printedHeader)
      {
        Serial.println("Topology notes:");
        printedHeader = true;
      }
      Serial.printf("  Hub port %u is connected, but no tracked device has parent/root port %u. A downstream hub may be flattened by the host stack.\n",
                    port,
                    port);
    }
    else if (!connected && rootPortHasDevice[port])
    {
      if (!printedHeader)
      {
        Serial.println("Topology notes:");
        printedHeader = true;
      }
      Serial.printf("  Tracked device uses root port %u, but hub port %u status is disconnected. This may be a downstream hub port encoded as a root port.\n",
                    port,
                    port);
    }
  }
}

static void printHostAddressProbe()
{
  uint8_t addresses[16] = {};
  const size_t count = usb.getHostDeviceAddresses(addresses, sizeof(addresses));
  Serial.println("----------- Host Address Probe -----------");
  Serial.printf("Host address count=%u\n", (unsigned)count);
  for (size_t i = 0; i < count; i++)
  {
    EspUsbHostDeviceProbeInfo probe;
    const bool ok = usb.probeHostDevice(addresses[i], probe);
    Serial.printf("Probe address=%u ok=%s open=%s devInfo=%s devDesc=%s cfgDesc=%s parent=%u parentPort=%u vid=%04x pid=%04x class=0x%02x sub=0x%02x proto=0x%02x interfaces=%u hubInterface=%s hubDesc=%s",
                  addresses[i],
                  yesNo(ok),
                  yesNo(probe.openOk),
                  yesNo(probe.deviceInfoOk),
                  yesNo(probe.deviceDescriptorOk),
                  yesNo(probe.configDescriptorOk),
                  probe.parentAddress,
                  probe.parentPort,
                  probe.vid,
                  probe.pid,
                  probe.deviceClass,
                  probe.deviceSubClass,
                  probe.deviceProtocol,
                  probe.interfaceCount,
                  yesNo(probe.configHasHubInterface),
                  yesNo(probe.hubDescriptorOk));
    if (probe.hubDescriptorOk)
    {
      Serial.printf(" hubPorts=%u hubChars=0x%04x ppps=%s",
                    probe.hub.portCount,
                    probe.hub.characteristics,
                    yesNo(probe.hub.perPortPowerSwitching));
    }
    Serial.println();
  }
  Serial.println("--------- Host Address Probe End ---------");
}

static void printDevice(uint8_t address, bool includeHubInfo = false)
{
  EspUsbHostDeviceInfo device;
  if (!usb.getDevice(address, device))
  {
    Serial.printf("Device address=%u not found\n", address);
    return;
  }

  Serial.println();
  Serial.println("=========== USB Device ===========");
  const uint8_t hubIndex = device.portId >> 4;
  const uint8_t upstreamPort = device.portId & 0x0f;
  Serial.printf("Address %u portId=0x%02x", device.address, device.portId);
  if (device.parentAddress)
  {
    Serial.printf(" parent=%u hub_index=%u upstream_port=%u", device.parentAddress, hubIndex, upstreamPort);
  }
  else
  {
    Serial.printf(" parent=root root_port=%u", device.portId);
    if (device.portId > 1)
    {
      Serial.print(" note=hub_stack_may_be_flattened");
    }
  }
  Serial.printf(" speed=%s\n", speedName(device.speed));
  Serial.printf("VID:PID %04x:%04x class=0x%02x(%s) subclass=0x%02x protocol=0x%02x\n",
                device.vid,
                device.pid,
                device.deviceClass,
                className(device.deviceClass),
                device.deviceSubClass,
                device.deviceProtocol);
  Serial.printf("Supported=%s hub=%s\n",
                yesNo(device.supported),
                yesNo(device.isHub));
  if (!device.supported)
  {
    Serial.println("Note: unsupported by this library, but descriptors are available for inspection.");
  }
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
  Serial.printf("Configuration value=%u interfaces=%u total_len=%u attributes=0x%02x(%s remote_wakeup=%s) max_power=%umA\n",
                device.configurationValue,
                device.configurationInterfaceCount,
                device.configurationTotalLength,
                device.configurationAttributes,
                configAttributeName(device.configurationAttributes),
                yesNo(device.configurationAttributes & 0x20),
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
  if (includeHubInfo && device.isHub)
  {
    printHubInfo(device.address, true);
  }
  Serial.println("========= USB Device End =========");
  Serial.println();
}

static void printAllDevices()
{
  Serial.println();
  Serial.println("=========== USB Topology ===========");
  EspUsbHostHubInfo rootHub;
  const bool hasRootHub = usb.getHubInfo(1, rootHub);
  if (hasRootHub)
  {
    printHubInfo(1, true);
  }
  else
  {
    Serial.println("Hub address=1 descriptor unavailable");
  }

  EspUsbHostDeviceInfo devices[ESP_USB_HOST_MAX_DEVICES];
  const size_t count = usb.getDevices(devices, ESP_USB_HOST_MAX_DEVICES);
  Serial.printf("Tracked devices=%u\n", (unsigned)count);
  if (hasRootHub)
  {
    printFlattenedHubNotes(rootHub, devices, count);
  }
  printHostAddressProbe();
  if (count == 0)
  {
    Serial.println("No USB devices");
    Serial.println("========= USB Topology End =========");
    return;
  }
  for (size_t i = 0; i < count; i++)
  {
    printDevice(devices[i].address, true);
  }
  Serial.println("========= USB Topology End =========");
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("EspUsbHostDeviceInfo start");
  Serial.println("Press 'r' to reprint connected devices and hub status.");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          lastDeviceEventMs = millis();
                          Serial.printf("CONNECTED address=%u\n", device.address);
                          printDevice(device.address);
                        });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             lastDeviceEventMs = millis();
                             Serial.printf("DISCONNECTED address=%u vid=%04x pid=%04x\n",
                                           device.address,
                                           device.vid,
                                           device.pid);
                           });

  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
  lastDeviceEventMs = millis();
}

void loop()
{
  if (Serial.available() > 0 && Serial.read() == 'r')
  {
    printAllDevices();
  }
  if (!printedInitialTopology && millis() - lastDeviceEventMs > 2000)
  {
    printedInitialTopology = true;
    printAllDevices();
  }
  delay(10);
}
