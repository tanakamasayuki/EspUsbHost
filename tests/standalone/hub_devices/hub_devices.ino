#include <Arduino.h>
#include <usb/usb_host.h>

#if __has_include(<rom/usb/usb_common.h>)
#include <rom/usb/usb_common.h>
#else
#define USB_INTERFACE_DESC 0x04
#define USB_ENDPOINT_DESC 0x05
#endif

static constexpr size_t MAX_DEVICES = 8;
static constexpr uint8_t USB_CLASS_HUB_VALUE = 0x09;
static constexpr uint8_t USB_CLASS_HID_VALUE = 0x03;
static constexpr uint8_t USB_CLASS_MSC_VALUE = 0x08;
static constexpr uint8_t USB_CLASS_AUDIO_VALUE = 0x01;
static constexpr uint8_t USB_CLASS_VENDOR_VALUE = 0xff;
static constexpr uint8_t USB_AUDIO_SUBCLASS_MIDI_STREAMING = 0x03;
static constexpr uint16_t CH340_VID = 0x1a86;
static constexpr uint16_t CH340_PID = 0x7523;

struct DeviceInfo
{
  bool used = false;
  usb_device_handle_t handle = nullptr;
  uint8_t address = 0;
  uint8_t parentPort = 0;
  usb_speed_t speed = USB_SPEED_FULL;
  uint16_t vid = 0;
  uint16_t pid = 0;
  uint8_t deviceClass = 0;
  const usb_config_desc_t *config = nullptr;
  bool isHub = false;
  bool isCh340 = false;
  bool hasKeyboard = false;
  bool hasMsc = false;
  bool hasMidi = false;
};

struct Ch340State
{
  DeviceInfo *device = nullptr;
  bool claimed = false;
  uint8_t interfaceNumber = 0;
  uint8_t inEndpoint = 0;
  uint8_t outEndpoint = 0;
  uint16_t inPacketSize = 0;
  uint16_t outPacketSize = 0;
  usb_transfer_t *inTransfer = nullptr;
  volatile size_t rxLength = 0;
  uint8_t rx[128] = {};
};

static usb_host_client_handle_t clientHandle = nullptr;
static TaskHandle_t hostTaskHandle = nullptr;
static TaskHandle_t clientTaskHandle = nullptr;
static volatile bool running = true;
static DeviceInfo devices[MAX_DEVICES];
static Ch340State ch340;
static int passCount = 0;
static int failCount = 0;

static const char *speedName(usb_speed_t speed)
{
  switch (speed)
  {
  case USB_SPEED_LOW:
    return "low";
  case USB_SPEED_FULL:
    return "full";
  case USB_SPEED_HIGH:
    return "high";
  default:
    return "unknown";
  }
}

static void pass(const char *name)
{
  Serial.print("PASS ");
  Serial.println(name);
  passCount++;
}

static void fail(const char *name)
{
  Serial.print("FAIL ");
  Serial.println(name);
  failCount++;
}

static void skip(const char *name, const char *reason)
{
  Serial.print("SKIP ");
  Serial.print(name);
  Serial.print(' ');
  Serial.println(reason);
}

static bool configHasInterface(const usb_config_desc_t *config, uint8_t cls, int subclass = -1, int protocol = -1)
{
  if (!config)
  {
    return false;
  }
  const uint8_t *p = reinterpret_cast<const uint8_t *>(config);
  for (int offset = 0; offset < config->wTotalLength;)
  {
    const uint8_t length = p[offset];
    if (length < 2 || offset + length > config->wTotalLength)
    {
      return false;
    }
    if (p[offset + 1] == USB_INTERFACE_DESC)
    {
      const usb_intf_desc_t *intf = reinterpret_cast<const usb_intf_desc_t *>(&p[offset]);
      if (intf->bInterfaceClass == cls &&
          (subclass < 0 || intf->bInterfaceSubClass == static_cast<uint8_t>(subclass)) &&
          (protocol < 0 || intf->bInterfaceProtocol == static_cast<uint8_t>(protocol)))
      {
        return true;
      }
    }
    offset += length;
  }
  return false;
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

static DeviceInfo *findCh340()
{
  for (DeviceInfo &device : devices)
  {
    if (device.used && device.isCh340)
    {
      return &device;
    }
  }
  return nullptr;
}

static bool submitControl(usb_device_handle_t handle, uint8_t request, uint16_t value, uint16_t index)
{
  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE, 0, &transfer);
  if (err != ESP_OK)
  {
    return false;
  }
  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = 0x40;
  setup->bRequest = request;
  setup->wValue = value;
  setup->wIndex = index;
  setup->wLength = 0;
  transfer->device_handle = handle;
  transfer->bEndpointAddress = 0;
  transfer->callback = [](usb_transfer_t *t)
  {
    usb_host_transfer_free(t);
  };
  transfer->context = nullptr;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE;
  err = usb_host_transfer_submit_control(clientHandle, transfer);
  if (err != ESP_OK)
  {
    usb_host_transfer_free(transfer);
    return false;
  }
  delay(20);
  return true;
}

static void ch340InCallback(usb_transfer_t *transfer)
{
  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes > 0)
  {
    const size_t copyLength = min(static_cast<size_t>(transfer->actual_num_bytes), sizeof(ch340.rx) - ch340.rxLength);
    memcpy(ch340.rx + ch340.rxLength, transfer->data_buffer, copyLength);
    ch340.rxLength += copyLength;
  }
  if (running && ch340.device && transfer->status != USB_TRANSFER_STATUS_NO_DEVICE)
  {
    usb_host_transfer_submit(transfer);
  }
}

static bool setupCh340(DeviceInfo *device)
{
  if (ch340.claimed && ch340.device == device && ch340.inTransfer)
  {
    return true;
  }

  ch340 = Ch340State();
  ch340.device = device;

  const uint8_t *p = reinterpret_cast<const uint8_t *>(device->config);
  bool inCh340Interface = false;
  for (int offset = 0; offset < device->config->wTotalLength;)
  {
    const uint8_t length = p[offset];
    if (length < 2 || offset + length > device->config->wTotalLength)
    {
      return false;
    }
    if (p[offset + 1] == USB_INTERFACE_DESC)
    {
      const usb_intf_desc_t *intf = reinterpret_cast<const usb_intf_desc_t *>(&p[offset]);
      inCh340Interface = intf->bInterfaceClass == USB_CLASS_VENDOR_VALUE;
      if (inCh340Interface)
      {
        ch340.interfaceNumber = intf->bInterfaceNumber;
      }
    }
    else if (inCh340Interface && p[offset + 1] == USB_ENDPOINT_DESC)
    {
      const usb_ep_desc_t *ep = reinterpret_cast<const usb_ep_desc_t *>(&p[offset]);
      const bool isBulk = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_BULK;
      const bool isIn = (ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0;
      if (isBulk && isIn)
      {
        ch340.inEndpoint = ep->bEndpointAddress;
        ch340.inPacketSize = ep->wMaxPacketSize;
      }
      else if (isBulk && !isIn)
      {
        ch340.outEndpoint = ep->bEndpointAddress;
        ch340.outPacketSize = ep->wMaxPacketSize;
      }
    }
    offset += length;
  }

  if (!ch340.inEndpoint || !ch340.outEndpoint)
  {
    return false;
  }

  esp_err_t err = usb_host_interface_claim(clientHandle, device->handle, ch340.interfaceNumber, 0);
  if (err != ESP_OK)
  {
    Serial.printf("WARN ch340_claim_failed %s\n", esp_err_to_name(err));
    return false;
  }
  ch340.claimed = true;

  submitControl(device->handle, 0xa1, 0x0000, 0x0000);
  submitControl(device->handle, 0x9a, 0x1312, 0xd980);
  submitControl(device->handle, 0x9a, 0x2518, 0x00c3);
  submitControl(device->handle, 0xa4, 0x0060, ch340.interfaceNumber);

  err = usb_host_transfer_alloc(ch340.inPacketSize, 0, &ch340.inTransfer);
  if (err != ESP_OK)
  {
    return false;
  }
  ch340.inTransfer->device_handle = device->handle;
  ch340.inTransfer->bEndpointAddress = ch340.inEndpoint;
  ch340.inTransfer->callback = ch340InCallback;
  ch340.inTransfer->context = nullptr;
  ch340.inTransfer->num_bytes = ch340.inPacketSize;
  err = usb_host_transfer_submit(ch340.inTransfer);
  return err == ESP_OK;
}

static bool sendCh340(const uint8_t *data, size_t length)
{
  usb_transfer_t *transfer = nullptr;
  const size_t packetSize = max(length, static_cast<size_t>(ch340.outPacketSize));
  esp_err_t err = usb_host_transfer_alloc(packetSize, 0, &transfer);
  if (err != ESP_OK)
  {
    return false;
  }
  memcpy(transfer->data_buffer, data, length);
  transfer->device_handle = ch340.device->handle;
  transfer->bEndpointAddress = ch340.outEndpoint;
  transfer->callback = [](usb_transfer_t *t)
  {
    usb_host_transfer_free(t);
  };
  transfer->context = nullptr;
  transfer->num_bytes = length;
  err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    usb_host_transfer_free(transfer);
    return false;
  }
  return true;
}

static bool bufferContains(const uint8_t *buffer, size_t bufferLength, const char *text)
{
  const size_t textLength = strlen(text);
  if (textLength == 0 || bufferLength < textLength)
  {
    return false;
  }
  for (size_t i = 0; i + textLength <= bufferLength; i++)
  {
    if (memcmp(buffer + i, text, textLength) == 0)
    {
      return true;
    }
  }
  return false;
}

static void addDevice(uint8_t address)
{
  const int slot = findFreeDeviceSlot();
  if (slot < 0)
  {
    Serial.printf("WARN device_slot_full address=%u\n", address);
    return;
  }

  usb_device_handle_t handle = nullptr;
  esp_err_t err = usb_host_device_open(clientHandle, address, &handle);
  if (err != ESP_OK)
  {
    Serial.printf("WARN open_failed address=%u err=%s\n", address, esp_err_to_name(err));
    return;
  }

  usb_device_info_t info = {};
  const usb_device_desc_t *desc = nullptr;
  const usb_config_desc_t *config = nullptr;
  usb_host_device_info(handle, &info);
  usb_host_get_device_descriptor(handle, &desc);
  usb_host_get_active_config_descriptor(handle, &config);

  DeviceInfo &device = devices[slot];
  device = DeviceInfo();
  device.used = true;
  device.handle = handle;
  device.address = info.dev_addr;
  device.parentPort = info.parent.port_num;
  device.speed = info.speed;
  device.vid = desc ? desc->idVendor : 0;
  device.pid = desc ? desc->idProduct : 0;
  device.deviceClass = desc ? desc->bDeviceClass : 0;
  device.config = config;
  device.isHub = device.deviceClass == USB_CLASS_HUB_VALUE || configHasInterface(config, USB_CLASS_HUB_VALUE);
  device.isCh340 = device.vid == CH340_VID && device.pid == CH340_PID;
  device.hasKeyboard = configHasInterface(config, USB_CLASS_HID_VALUE, 0x01, 0x01);
  device.hasMsc = configHasInterface(config, USB_CLASS_MSC_VALUE);
  device.hasMidi = configHasInterface(config, USB_CLASS_AUDIO_VALUE, USB_AUDIO_SUBCLASS_MIDI_STREAMING);

  Serial.printf("DEVICE address=%u port=%u speed=%s vid=%04x pid=%04x hub=%u ch340=%u keyboard=%u msc=%u midi=%u\n",
                device.address,
                device.parentPort,
                speedName(device.speed),
                device.vid,
                device.pid,
                device.isHub ? 1 : 0,
                device.isCh340 ? 1 : 0,
                device.hasKeyboard ? 1 : 0,
                device.hasMsc ? 1 : 0,
                device.hasMidi ? 1 : 0);
}

static void clientEventCallback(const usb_host_client_event_msg_t *eventMsg, void *)
{
  if (eventMsg->event == USB_HOST_CLIENT_EVENT_NEW_DEV)
  {
    addDevice(eventMsg->new_dev.address);
  }
}

static void hostTask(void *)
{
  usb_host_config_t hostConfig = {};
  hostConfig.skip_phy_setup = false;
  hostConfig.intr_flags = ESP_INTR_FLAG_LOWMED;
  if (usb_host_install(&hostConfig) != ESP_OK)
  {
    fail("usb_host_install");
    vTaskDelete(nullptr);
  }
  while (running)
  {
    uint32_t eventFlags = 0;
    usb_host_lib_handle_events(pdMS_TO_TICKS(100), &eventFlags);
  }
  usb_host_uninstall();
  vTaskDelete(nullptr);
}

static void clientTask(void *)
{
  usb_host_client_config_t clientConfig = {};
  clientConfig.is_synchronous = false;
  clientConfig.max_num_event_msg = 16;
  clientConfig.async.client_event_callback = clientEventCallback;
  clientConfig.async.callback_arg = nullptr;
  if (usb_host_client_register(&clientConfig, &clientHandle) != ESP_OK)
  {
    fail("usb_host_client_register");
    vTaskDelete(nullptr);
  }
  while (running)
  {
    usb_host_client_handle_events(clientHandle, pdMS_TO_TICKS(100));
  }
  usb_host_client_deregister(clientHandle);
  vTaskDelete(nullptr);
}

static void runInventory()
{
  bool hasCh340 = false;
  bool hasKeyboard = false;
  bool hasMsc = false;
  bool hasMidi = false;
  for (const DeviceInfo &device : devices)
  {
    hasCh340 |= device.used && device.isCh340;
    hasKeyboard |= device.used && device.hasKeyboard;
    hasMsc |= device.used && device.hasMsc;
    hasMidi |= device.used && device.hasMidi;
  }

  if (hasCh340)
    pass("inventory_ch340_present");
  else
    skip("inventory_ch340", "not_connected");
  if (hasKeyboard)
    pass("inventory_keyboard_present");
  else
    skip("inventory_keyboard", "not_connected");
  if (hasMsc)
    pass("inventory_msc_present");
  else
    skip("inventory_msc", "not_connected");
  if (hasMidi)
    pass("inventory_midi_present");
  else
    skip("inventory_midi", "not_connected");

  Serial.println("INVENTORY_DONE");
}

static void runCh340Loopback()
{
  DeviceInfo *device = findCh340();
  if (!device)
  {
    skip("ch340_loopback", "not_connected");
    return;
  }
  if (!setupCh340(device))
  {
    fail("ch340_setup");
    return;
  }

  static const char payload[] = "EspUsbHost CH340 loopback\n";
  ch340.rxLength = 0;
  memset(ch340.rx, 0, sizeof(ch340.rx));
  if (!sendCh340(reinterpret_cast<const uint8_t *>(payload), strlen(payload)))
  {
    fail("ch340_send");
    return;
  }

  const uint32_t start = millis();
  while (millis() - start < 3000)
  {
    if (ch340.rxLength >= strlen(payload) &&
        bufferContains(ch340.rx, ch340.rxLength, payload))
    {
      pass("ch340_loopback");
      return;
    }
    delay(10);
  }
  Serial.printf("WARN ch340_rx_len=%u data=", static_cast<unsigned>(ch340.rxLength));
  for (size_t i = 0; i < ch340.rxLength; i++)
  {
    Serial.printf("%02x", ch340.rx[i]);
  }
  Serial.println();
  fail("ch340_loopback");
}

static void beginTest(const char *name)
{
  passCount = 0;
  failCount = 0;
  Serial.print("TEST_BEGIN ");
  Serial.println(name);
}

static void endTest()
{
  Serial.printf("TEST_END pass=%d fail=%d\n", passCount, failCount);
  Serial.println(failCount == 0 ? "OK" : "NG");
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  xTaskCreate(hostTask, "hubHost", 4096, nullptr, 5, &hostTaskHandle);
  delay(50);
  xTaskCreate(clientTask, "hubClient", 8192, nullptr, 5, &clientTaskHandle);

  delay(12000);
  Serial.println("READY hub_devices");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'i')
    {
      beginTest("hub_inventory");
      runInventory();
      endTest();
    }
    else if (command == 'c')
    {
      beginTest("ch340_loopback");
      runCh340Loopback();
      endTest();
    }
  }
  delay(10);
}
