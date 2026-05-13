#include <Arduino.h>
#include <usb/usb_host.h>

#if __has_include(<rom/usb/usb_common.h>)
#include <rom/usb/usb_common.h>
#else
#define USB_DEVICE_DESC 0x01
#define USB_CONFIGURATION_DESC 0x02
#define USB_STRING_DESC 0x03
#define USB_INTERFACE_DESC 0x04
#define USB_ENDPOINT_DESC 0x05
#define USB_INTERFACE_ASSOC_DESC 0x0B
#define USB_HID_DESC 0x21
#define USB_HID_REPORT_DESC 0x22
#endif

static constexpr size_t MAX_DEVICES = 16;

struct DeviceNode
{
  bool used = false;
  usb_device_handle_t handle = nullptr;
  usb_device_handle_t parent = nullptr;
  uint8_t parentPort = 0;
  uint8_t address = 0;
  usb_speed_t speed = USB_SPEED_FULL;
  uint8_t maxPacketSize0 = 0;
  uint8_t configurationValue = 0;
  const usb_device_desc_t *deviceDesc = nullptr;
  const usb_config_desc_t *configDesc = nullptr;
  String manufacturer;
  String product;
  String serial;
};

static usb_host_client_handle_t clientHandle = nullptr;
static TaskHandle_t hostTaskHandle = nullptr;
static TaskHandle_t clientTaskHandle = nullptr;
static volatile bool running = true;
static SemaphoreHandle_t devicesMutex = nullptr;
static DeviceNode devices[MAX_DEVICES];
static volatile bool printRequested = false;

static String usbString(const usb_str_desc_t *strDesc)
{
  String result;
  if (!strDesc)
  {
    return result;
  }
  for (int i = 0; i < strDesc->bLength / 2; i++)
  {
    if (strDesc->wData[i] <= 0xff)
    {
      result += static_cast<char>(strDesc->wData[i]);
    }
  }
  return result;
}

static const char *speedName(usb_speed_t speed)
{
  switch (speed)
  {
  case USB_SPEED_LOW:
    return "1.5M low-speed";
  case USB_SPEED_FULL:
    return "12M full-speed";
  case USB_SPEED_HIGH:
    return "480M high-speed";
  default:
    return "unknown-speed";
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

static const char *transferTypeName(uint8_t attrs)
{
  switch (attrs & USB_BM_ATTRIBUTES_XFERTYPE_MASK)
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

static const char *descriptorTypeName(uint8_t type)
{
  switch (type)
  {
  case USB_DEVICE_DESC:
    return "Device";
  case USB_CONFIGURATION_DESC:
    return "Configuration";
  case USB_STRING_DESC:
    return "String";
  case USB_INTERFACE_DESC:
    return "Interface";
  case USB_ENDPOINT_DESC:
    return "Endpoint";
  case USB_INTERFACE_ASSOC_DESC:
    return "IAD";
  case USB_HID_DESC:
    return "HID";
  case USB_HID_REPORT_DESC:
    return "HID Report";
  default:
    return "Class/Vendor";
  }
}

static int findDeviceByHandle(usb_device_handle_t handle)
{
  for (size_t i = 0; i < MAX_DEVICES; i++)
  {
    if (devices[i].used && devices[i].handle == handle)
    {
      return static_cast<int>(i);
    }
  }
  return -1;
}

static int findFreeDeviceSlot()
{
  for (size_t i = 0; i < MAX_DEVICES; i++)
  {
    if (!devices[i].used)
    {
      return static_cast<int>(i);
    }
  }
  return -1;
}

static void printIndent(uint8_t depth)
{
  for (uint8_t i = 0; i < depth; i++)
  {
    Serial.print("  ");
  }
}

static void printDeviceDescriptor(const DeviceNode &node, uint8_t depth)
{
  const usb_device_desc_t *d = node.deviceDesc;
  if (!d)
  {
    return;
  }

  printIndent(depth);
  Serial.printf("Device Descriptor: usb=%x.%02x class=0x%02x(%s) subclass=0x%02x protocol=0x%02x max_ep0=%u configs=%u\n",
                d->bcdUSB >> 8,
                d->bcdUSB & 0xff,
                d->bDeviceClass,
                className(d->bDeviceClass),
                d->bDeviceSubClass,
                d->bDeviceProtocol,
                d->bMaxPacketSize0,
                d->bNumConfigurations);
  printIndent(depth);
  Serial.printf("IDs: vid=%04x pid=%04x device=%x.%02x iManufacturer=%u iProduct=%u iSerial=%u\n",
                d->idVendor,
                d->idProduct,
                d->bcdDevice >> 8,
                d->bcdDevice & 0xff,
                d->iManufacturer,
                d->iProduct,
                d->iSerialNumber);
}

static void printConfigDescriptor(const DeviceNode &node, uint8_t depth)
{
  const usb_config_desc_t *config = node.configDesc;
  if (!config)
  {
    return;
  }

  const bool selfPowered = (config->bmAttributes & 0x40) != 0;
  const bool remoteWakeup = (config->bmAttributes & 0x20) != 0;
  printIndent(depth);
  Serial.printf("Configuration: value=%u interfaces=%u total_len=%u attributes=0x%02x %s remote_wakeup=%u max_power=%umA\n",
                config->bConfigurationValue,
                config->bNumInterfaces,
                config->wTotalLength,
                config->bmAttributes,
                selfPowered ? "self-powered" : "bus-powered",
                remoteWakeup ? 1 : 0,
                config->bMaxPower * 2);

  const uint8_t *p = reinterpret_cast<const uint8_t *>(config);
  uint8_t currentInterface = 0xff;
  for (int offset = 0; offset < config->wTotalLength;)
  {
    const uint8_t length = p[offset];
    const uint8_t type = p[offset + 1];
    if (length < 2 || offset + length > config->wTotalLength)
    {
      printIndent(depth + 1);
      Serial.printf("Invalid descriptor at offset=%d length=%u\n", offset, length);
      return;
    }

    if (type == USB_INTERFACE_DESC)
    {
      const usb_intf_desc_t *intf = reinterpret_cast<const usb_intf_desc_t *>(&p[offset]);
      currentInterface = intf->bInterfaceNumber;
      printIndent(depth + 1);
      Serial.printf("Interface %u alt=%u class=0x%02x(%s) subclass=0x%02x protocol=0x%02x endpoints=%u iInterface=%u\n",
                    intf->bInterfaceNumber,
                    intf->bAlternateSetting,
                    intf->bInterfaceClass,
                    className(intf->bInterfaceClass),
                    intf->bInterfaceSubClass,
                    intf->bInterfaceProtocol,
                    intf->bNumEndpoints,
                    intf->iInterface);
    }
    else if (type == USB_ENDPOINT_DESC)
    {
      const usb_ep_desc_t *ep = reinterpret_cast<const usb_ep_desc_t *>(&p[offset]);
      printIndent(depth + 2);
      Serial.printf("Endpoint iface=%u ep=0x%02x dir=%s type=%s max_packet=%u interval=%u attrs=0x%02x\n",
                    currentInterface,
                    ep->bEndpointAddress,
                    (ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) ? "IN" : "OUT",
                    transferTypeName(ep->bmAttributes),
                    ep->wMaxPacketSize,
                    ep->bInterval,
                    ep->bmAttributes);
    }
    else if (type == USB_INTERFACE_ASSOC_DESC)
    {
      printIndent(depth + 1);
      Serial.printf("IAD len=%u raw=", length);
      for (uint8_t i = 0; i < length; i++)
      {
        Serial.printf("%02x", p[offset + i]);
        if (i + 1 < length)
        {
          Serial.print(' ');
        }
      }
      Serial.println();
    }
    else if (type != USB_CONFIGURATION_DESC)
    {
      printIndent(depth + 2);
      Serial.printf("%s descriptor type=0x%02x len=%u raw=", descriptorTypeName(type), type, length);
      for (uint8_t i = 0; i < length; i++)
      {
        Serial.printf("%02x", p[offset + i]);
        if (i + 1 < length)
        {
          Serial.print(' ');
        }
      }
      Serial.println();
    }

    offset += length;
  }
}

static void printNode(int index, uint8_t depth)
{
  const DeviceNode &node = devices[index];
  const usb_device_desc_t *d = node.deviceDesc;

  printIndent(depth);
  Serial.printf("|__ Dev %u Port %u: %s, cfg=%u, ep0=%u",
                node.address,
                node.parentPort,
                speedName(node.speed),
                node.configurationValue,
                node.maxPacketSize0);
  if (d)
  {
    Serial.printf(", vid=%04x pid=%04x class=0x%02x(%s)",
                  d->idVendor,
                  d->idProduct,
                  d->bDeviceClass,
                  className(d->bDeviceClass));
  }
  Serial.println();

  printIndent(depth + 1);
  Serial.printf("Strings: manufacturer=\"%s\" product=\"%s\" serial=\"%s\"\n",
                node.manufacturer.c_str(),
                node.product.c_str(),
                node.serial.c_str());

  printDeviceDescriptor(node, depth + 1);
  printConfigDescriptor(node, depth + 1);

  for (size_t i = 0; i < MAX_DEVICES; i++)
  {
    if (devices[i].used && devices[i].parent == node.handle)
    {
      printNode(static_cast<int>(i), depth + 1);
    }
  }
}

static void printTree()
{
  xSemaphoreTake(devicesMutex, portMAX_DELAY);
  Serial.println();
  Serial.println("=========== USB Topology ===========");
  Serial.println("/:  Bus 001: ESP32 USB Host root");

  bool printed = false;
  for (size_t i = 0; i < MAX_DEVICES; i++)
  {
    if (devices[i].used && devices[i].parent == nullptr)
    {
      printNode(static_cast<int>(i), 1);
      printed = true;
    }
  }
  if (!printed)
  {
    Serial.println("  (no devices)");
  }
  Serial.println("========= USB Topology End =========");
  Serial.println();
  xSemaphoreGive(devicesMutex);
}

static void addDevice(uint8_t address)
{
  usb_device_handle_t handle = nullptr;
  esp_err_t err = usb_host_device_open(clientHandle, address, &handle);
  if (err != ESP_OK)
  {
    Serial.printf("open dev %u failed: %s\n", address, esp_err_to_name(err));
    return;
  }

  usb_device_info_t info = {};
  const usb_device_desc_t *deviceDesc = nullptr;
  const usb_config_desc_t *configDesc = nullptr;
  err = usb_host_device_info(handle, &info);
  if (err != ESP_OK)
  {
    Serial.printf("device_info dev %u failed: %s\n", address, esp_err_to_name(err));
  }
  err = usb_host_get_device_descriptor(handle, &deviceDesc);
  if (err != ESP_OK)
  {
    Serial.printf("device_descriptor dev %u failed: %s\n", address, esp_err_to_name(err));
  }
  err = usb_host_get_active_config_descriptor(handle, &configDesc);
  if (err != ESP_OK)
  {
    Serial.printf("config_descriptor dev %u failed: %s\n", address, esp_err_to_name(err));
  }

  xSemaphoreTake(devicesMutex, portMAX_DELAY);
  int slot = findFreeDeviceSlot();
  if (slot >= 0)
  {
    devices[slot] = DeviceNode();
    devices[slot].used = true;
    devices[slot].handle = handle;
    devices[slot].parent = info.parent.dev_hdl;
    devices[slot].parentPort = info.parent.port_num;
    devices[slot].address = info.dev_addr;
    devices[slot].speed = info.speed;
    devices[slot].maxPacketSize0 = info.bMaxPacketSize0;
    devices[slot].configurationValue = info.bConfigurationValue;
    devices[slot].deviceDesc = deviceDesc;
    devices[slot].configDesc = configDesc;
    devices[slot].manufacturer = usbString(info.str_desc_manufacturer);
    devices[slot].product = usbString(info.str_desc_product);
    devices[slot].serial = usbString(info.str_desc_serial_num);
  }
  xSemaphoreGive(devicesMutex);

  if (slot < 0)
  {
    Serial.printf("No device slots available for address=%u\n", address);
    usb_host_device_close(clientHandle, handle);
  }
  else
  {
    Serial.printf("NEW_DEV address=%u slot=%d\n", address, slot);
  }
  printRequested = true;
}

static void removeDevice(usb_device_handle_t handle)
{
  usb_device_handle_t closeHandle = nullptr;
  xSemaphoreTake(devicesMutex, portMAX_DELAY);
  const int slot = findDeviceByHandle(handle);
  if (slot >= 0)
  {
    closeHandle = devices[slot].handle;
    devices[slot] = DeviceNode();
  }
  xSemaphoreGive(devicesMutex);

  if (closeHandle)
  {
    usb_host_device_close(clientHandle, closeHandle);
    Serial.printf("DEV_GONE slot=%d\n", slot);
  }
  else
  {
    Serial.println("DEV_GONE unknown");
  }
  printRequested = true;
}

static void clientEventCallback(const usb_host_client_event_msg_t *eventMsg, void *)
{
  switch (eventMsg->event)
  {
  case USB_HOST_CLIENT_EVENT_NEW_DEV:
    addDevice(eventMsg->new_dev.address);
    break;
  case USB_HOST_CLIENT_EVENT_DEV_GONE:
    removeDevice(eventMsg->dev_gone.dev_hdl);
    break;
  default:
    Serial.printf("client event=%d\n", eventMsg->event);
    break;
  }
}

static void hostTask(void *)
{
  usb_host_config_t hostConfig = {};
  hostConfig.skip_phy_setup = false;
  hostConfig.intr_flags = ESP_INTR_FLAG_LOWMED;

  esp_err_t err = usb_host_install(&hostConfig);
  if (err != ESP_OK)
  {
    Serial.printf("usb_host_install failed: %s\n", esp_err_to_name(err));
    vTaskDelete(nullptr);
    return;
  }
  Serial.println("usb_host_install OK");

  while (running)
  {
    uint32_t eventFlags = 0;
    err = usb_host_lib_handle_events(portMAX_DELAY, &eventFlags);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
    {
      Serial.printf("usb_host_lib_handle_events failed: %s flags=0x%08lx\n",
                    esp_err_to_name(err),
                    static_cast<unsigned long>(eventFlags));
    }
  }

  usb_host_uninstall();
  hostTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

static void clientTask(void *)
{
  usb_host_client_config_t clientConfig = {};
  clientConfig.is_synchronous = false;
  clientConfig.max_num_event_msg = 16;
  clientConfig.async.client_event_callback = clientEventCallback;
  clientConfig.async.callback_arg = nullptr;

  esp_err_t err = usb_host_client_register(&clientConfig, &clientHandle);
  if (err != ESP_OK)
  {
    Serial.printf("usb_host_client_register failed: %s\n", esp_err_to_name(err));
    vTaskDelete(nullptr);
    return;
  }
  Serial.println("usb_host_client_register OK");

  while (running)
  {
    err = usb_host_client_handle_events(clientHandle, portMAX_DELAY);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
    {
      Serial.printf("usb_host_client_handle_events failed: %s\n", esp_err_to_name(err));
    }
  }

  for (size_t i = 0; i < MAX_DEVICES; i++)
  {
    if (devices[i].used && devices[i].handle)
    {
      usb_host_device_close(clientHandle, devices[i].handle);
      devices[i] = DeviceNode();
    }
  }
  usb_host_client_deregister(clientHandle);
  clientHandle = nullptr;
  clientTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void setup()
{
  Serial.begin(115200);
  delay(5000);
  devicesMutex = xSemaphoreCreateMutex();
  Serial.println("EspUsbHostLsUsb start");
  Serial.println("Press 'r' to reprint the current topology.");

  xTaskCreate(hostTask, "LsUsbHost", 4096, nullptr, 5, &hostTaskHandle);
  delay(50);
  xTaskCreate(clientTask, "LsUsbClient", 8192, nullptr, 5, &clientTaskHandle);
}

void loop()
{
  if (Serial.available() > 0)
  {
    char command = Serial.read();
    if (command == 'r')
    {
      printRequested = true;
    }
  }

  if (printRequested)
  {
    printRequested = false;
    delay(200);
    printTree();
  }
  delay(10);
}
