#include "EspUsbHost.h"
#include "EspUsbHostHid.h"

static const char *TAG __attribute__((unused)) = "EspUsbHost";

static constexpr uint8_t USB_CLASS_HID_VALUE = 0x03;
static constexpr uint8_t USB_CLASS_HUB_VALUE = 0x09;
static constexpr uint8_t USB_CLASS_CDC_CONTROL_VALUE = 0x02;
static constexpr uint8_t USB_CLASS_CDC_DATA_VALUE = 0x0a;
static constexpr uint8_t USB_CLASS_VENDOR_VALUE = 0xff;
static constexpr uint8_t HID_SUBCLASS_BOOT_VALUE = 0x01;
static constexpr uint8_t HID_PROTOCOL_KEYBOARD_VALUE = 0x01;
static constexpr uint8_t HID_PROTOCOL_MOUSE_VALUE = 0x02;
static constexpr uint8_t HID_CLASS_REQUEST_SET_REPORT = 0x09;
static constexpr uint8_t HID_SET_REPORT_REQUEST_TYPE = 0x21;
static constexpr uint8_t CDC_CLASS_REQUEST_SET_LINE_CODING = 0x20;
static constexpr uint8_t CDC_CLASS_REQUEST_SET_CONTROL_LINE_STATE = 0x22;
static constexpr uint8_t CDC_SET_REQUEST_TYPE = 0x21;
static constexpr uint8_t VENDOR_OUT_REQUEST_TYPE = 0x40;
static constexpr uint8_t VENDOR_INTERFACE_OUT_REQUEST_TYPE = 0x41;

static bool isKnownVendorSerial(uint16_t vid, uint16_t pid)
{
  switch (vid)
  {
  case 0x0403:
    return pid == 0x6001 || pid == 0x6010 || pid == 0x6011 || pid == 0x6014 || pid == 0x6015;
  case 0x10c4:
    return pid == 0xea60 || pid == 0xea70 || pid == 0xea71;
  case 0x1a86:
    return pid == 0x5523 || pid == 0x7522 || pid == 0x7523;
  default:
    return false;
  }
}

static const char *vendorSerialName(uint16_t vid)
{
  switch (vid)
  {
  case 0x0403:
    return "FTDI";
  case 0x10c4:
    return "CP210x";
  case 0x1a86:
    return "CH34x";
  default:
    return "vendor";
  }
}

static bool configHasInterfaceClass(const usb_config_desc_t *configDesc, uint8_t interfaceClass)
{
  const uint8_t *p = reinterpret_cast<const uint8_t *>(configDesc);
  for (int i = 0; i < configDesc->wTotalLength;)
  {
    const uint8_t length = p[i];
    if (length < 2 || i + length > configDesc->wTotalLength)
    {
      return false;
    }
    if (p[i + 1] == USB_INTERFACE_DESC)
    {
      const usb_intf_desc_t *intf = reinterpret_cast<const usb_intf_desc_t *>(&p[i]);
      if (intf->bInterfaceClass == interfaceClass)
      {
        return true;
      }
    }
    i += length;
  }
  return false;
}

EspUsbHost::EspUsbHost() = default;

EspUsbHost::~EspUsbHost()
{
  end();
}

bool EspUsbHost::begin()
{
  return begin(EspUsbHostConfig());
}

bool EspUsbHost::begin(const EspUsbHostConfig &config)
{
  if (running_)
  {
    ESP_LOGW(TAG, "begin() called while USB Host is already running");
    return true;
  }

  config_ = config;
  running_ = true;
  ready_ = false;
  lastError_ = ESP_OK;

  BaseType_t created;
  if (config_.taskCore == tskNO_AFFINITY)
  {
    created = xTaskCreate(taskEntry, "EspUsbHost", config_.taskStackSize, this, config_.taskPriority, &taskHandle_);
  }
  else
  {
    created = xTaskCreatePinnedToCore(taskEntry, "EspUsbHost", config_.taskStackSize, this, config_.taskPriority, &taskHandle_, config_.taskCore);
  }

  if (created != pdPASS)
  {
    running_ = false;
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  const uint32_t start = millis();
  while (running_ && !ready_ && millis() - start < 1000)
  {
    delay(1);
  }
  return ready_;
}

void EspUsbHost::end()
{
  if (!running_)
  {
    return;
  }

  ESP_LOGI(TAG, "Stopping USB Host");
  running_ = false;
  if (clientTaskHandle_)
  {
    for (int i = 0; i < 200 && clientTaskHandle_; i++)
    {
      delay(1);
    }
    if (clientTaskHandle_)
    {
      vTaskDelete(clientTaskHandle_);
      clientTaskHandle_ = nullptr;
    }
  }
  if (taskHandle_)
  {
    for (int i = 0; i < 200 && taskHandle_; i++)
    {
      delay(1);
    }
    if (taskHandle_)
    {
      vTaskDelete(taskHandle_);
      taskHandle_ = nullptr;
    }
  }
  ready_ = false;
}

bool EspUsbHost::ready() const
{
  return ready_;
}

void EspUsbHost::onDeviceConnected(DeviceCallback callback)
{
  deviceConnectedCallback_ = callback;
}

void EspUsbHost::onDeviceDisconnected(DeviceCallback callback)
{
  deviceDisconnectedCallback_ = callback;
}

void EspUsbHost::onKeyboard(KeyboardCallback callback)
{
  keyboardCallback_ = callback;
}

void EspUsbHost::onMouse(MouseCallback callback)
{
  mouseCallback_ = callback;
}

void EspUsbHost::onHIDInput(HIDInputCallback callback)
{
  hidInputCallback_ = callback;
}

void EspUsbHost::onSerialData(SerialDataCallback callback)
{
  serialDataCallback_ = callback;
}

void EspUsbHost::onConsumerControl(ConsumerControlCallback callback)
{
  consumerControlCallback_ = callback;
}

void EspUsbHost::onGamepad(GamepadCallback callback)
{
  gamepadCallback_ = callback;
}

void EspUsbHost::onVendorInput(VendorInputCallback callback)
{
  vendorInputCallback_ = callback;
}

void EspUsbHost::onSystemControl(SystemControlCallback callback)
{
  systemControlCallback_ = callback;
}

void EspUsbHost::setKeyboardLayout(EspUsbHostKeyboardLayout layout)
{
  keyboardLayout_ = layout;
}

bool EspUsbHost::sendHIDReport(uint8_t interfaceNumber,
                               uint8_t reportType,
                               uint8_t reportId,
                               const uint8_t *data,
                               size_t length)
{
  if (!running_ || !deviceHandle_ || !clientHandle_)
  {
    ESP_LOGW(TAG, "sendHIDReport() called while no HID device is open");
    return false;
  }
  if (length > 0 && !data)
  {
    ESP_LOGW(TAG, "sendHIDReport() called with null data");
    return false;
  }
  if (reportType != ESP_USB_HOST_HID_REPORT_TYPE_OUTPUT &&
      reportType != ESP_USB_HOST_HID_REPORT_TYPE_FEATURE)
  {
    ESP_LOGW(TAG, "sendHIDReport() unsupported reportType=%u", reportType);
    return false;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + length, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(control) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = HID_SET_REPORT_REQUEST_TYPE;
  setup->bRequest = HID_CLASS_REQUEST_SET_REPORT;
  setup->wValue = (static_cast<uint16_t>(reportType) << 8) | reportId;
  setup->wIndex = interfaceNumber;
  setup->wLength = length;
  if (length > 0)
  {
    memcpy(transfer->data_buffer + USB_SETUP_PACKET_SIZE, data, length);
  }

  transfer->device_handle = deviceHandle_;
  transfer->bEndpointAddress = 0;
  transfer->callback = controlTransferCallback;
  transfer->context = this;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE + length;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(Set_Report) failed: %s", esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }

  ESP_LOGD(TAG, "HID Set_Report submitted iface=%u type=%u id=%u length=%u",
           interfaceNumber,
           reportType,
           reportId,
           static_cast<unsigned>(length));
  return true;
}

bool EspUsbHost::setKeyboardLeds(bool numLock, bool capsLock, bool scrollLock)
{
  if (!hasKeyboardInterface_)
  {
    ESP_LOGW(TAG, "setKeyboardLeds() called before a keyboard interface is ready");
    return false;
  }

  uint8_t leds = espUsbHostBuildKeyboardLedReport(numLock, capsLock, scrollLock);

  return sendHIDReport(keyboardInterfaceNumber_,
                       ESP_USB_HOST_HID_REPORT_TYPE_OUTPUT,
                       0,
                       &leds,
                       sizeof(leds));
}

bool EspUsbHost::sendVendorOutput(const uint8_t *data, size_t length)
{
  if (!hasVendorInterface_)
  {
    ESP_LOGW(TAG, "sendVendorOutput() called before a vendor HID interface is ready");
    return false;
  }
  if (length > 63)
  {
    ESP_LOGW(TAG, "sendVendorOutput() length too large: %u", static_cast<unsigned>(length));
    return false;
  }
  uint8_t report[64] = {};
  report[0] = ESP_USB_HOST_HID_REPORT_ID_VENDOR;
  if (length > 0)
  {
    memcpy(report + 1, data, length);
  }

  return sendHIDReport(vendorInterfaceNumber_,
                       ESP_USB_HOST_HID_REPORT_TYPE_OUTPUT,
                       0,
                       report,
                       length + 1);
}

bool EspUsbHost::sendVendorFeature(const uint8_t *data, size_t length)
{
  if (!hasVendorInterface_)
  {
    ESP_LOGW(TAG, "sendVendorFeature() called before a vendor HID interface is ready");
    return false;
  }
  return sendHIDReport(vendorInterfaceNumber_,
                       ESP_USB_HOST_HID_REPORT_TYPE_FEATURE,
                       ESP_USB_HOST_HID_REPORT_ID_VENDOR,
                       data,
                       length);
}

bool EspUsbHost::sendSerial(const uint8_t *data, size_t length)
{
  if (!hasSerialOutEndpoint_ || !deviceHandle_)
  {
    ESP_LOGW(TAG, "sendSerial() called before a CDC OUT endpoint is ready");
    return false;
  }
  if (length > 0 && !data)
  {
    ESP_LOGW(TAG, "sendSerial() called with null data");
    return false;
  }

  const size_t packetSize = length > serialOutPacketSize_ ? length : serialOutPacketSize_;
  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(packetSize, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(serial OUT) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  if (length > 0)
  {
    memcpy(transfer->data_buffer, data, length);
  }
  transfer->device_handle = deviceHandle_;
  transfer->bEndpointAddress = serialOutEndpointAddress_;
  transfer->callback = serialOutTransferCallback;
  transfer->context = this;
  transfer->num_bytes = length;

  err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(serial OUT) failed: %s", esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }
  return true;
}

bool EspUsbHost::sendSerial(const char *text)
{
  if (!text)
  {
    return false;
  }
  return sendSerial(reinterpret_cast<const uint8_t *>(text), strlen(text));
}

bool EspUsbHost::serialReady() const
{
  return hasSerialOutEndpoint_ && deviceHandle_;
}

bool EspUsbHost::setSerialBaudRate(uint32_t baud)
{
  if (baud == 0)
  {
    ESP_LOGW(TAG, "setSerialBaudRate() called with baud=0");
    return false;
  }

  serialBaudRate_ = baud;
  if (hasCdcControlInterface_)
  {
    cdcConfigured_ = false;
    configureCdcAcm();
  }
  else if (vendorSerialSupported_)
  {
    configureVendorSerial();
  }
  return true;
}

int EspUsbHost::lastError() const
{
  return lastError_;
}

const char *EspUsbHost::lastErrorName() const
{
  return esp_err_to_name(lastError_);
}

void EspUsbHost::taskEntry(void *arg)
{
  static_cast<EspUsbHost *>(arg)->taskLoop();
}

void EspUsbHost::clientTaskEntry(void *arg)
{
  static_cast<EspUsbHost *>(arg)->clientTaskLoop();
}

void EspUsbHost::taskLoop()
{
  usb_host_config_t hostConfig = {};
  hostConfig.skip_phy_setup = false;
  hostConfig.intr_flags = ESP_INTR_FLAG_LOWMED;

  esp_err_t err = usb_host_install(&hostConfig);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "usb_host_install() failed: %s", esp_err_to_name(err));
    setLastError(err);
    running_ = false;
    taskHandle_ = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  usb_host_client_config_t clientConfig = {};
  clientConfig.is_synchronous = false;
  clientConfig.max_num_event_msg = 10;
  clientConfig.async.client_event_callback = clientEventCallback;
  clientConfig.async.callback_arg = this;

  err = usb_host_client_register(&clientConfig, &clientHandle_);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "usb_host_client_register() failed: %s", esp_err_to_name(err));
    setLastError(err);
    usb_host_uninstall();
    running_ = false;
    taskHandle_ = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  BaseType_t created;
  if (config_.taskCore == tskNO_AFFINITY)
  {
    created = xTaskCreate(clientTaskEntry, "EspUsbHostClient", config_.taskStackSize, this, config_.taskPriority, &clientTaskHandle_);
  }
  else
  {
    created = xTaskCreatePinnedToCore(clientTaskEntry, "EspUsbHostClient", config_.taskStackSize, this, config_.taskPriority, &clientTaskHandle_, config_.taskCore);
  }
  if (created != pdPASS)
  {
    ESP_LOGE(TAG, "Failed to create USB Host client task");
    setLastError(ESP_ERR_NO_MEM);
    running_ = false;
  }

  ready_ = running_;
  ESP_LOGI(TAG, "USB Host started stack=%lu priority=%u core=%d",
           static_cast<unsigned long>(config_.taskStackSize),
           static_cast<unsigned>(config_.taskPriority),
           static_cast<int>(config_.taskCore));

  while (running_)
  {
    uint32_t eventFlags = 0;
    err = usb_host_lib_handle_events(portMAX_DELAY, &eventFlags);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
    {
      ESP_LOGW(TAG, "usb_host_lib_handle_events() failed: %s", esp_err_to_name(err));
      setLastError(err);
    }
  }

  if (clientTaskHandle_)
  {
    vTaskDelete(clientTaskHandle_);
    clientTaskHandle_ = nullptr;
  }
  releaseEndpoints(true);
  releaseInterfaces();
  if (deviceHandle_)
  {
    usb_host_device_close(clientHandle_, deviceHandle_);
    deviceHandle_ = nullptr;
  }
  if (clientHandle_)
  {
    usb_host_client_deregister(clientHandle_);
    clientHandle_ = nullptr;
  }
  usb_host_device_free_all();
  usb_host_uninstall();

  ready_ = false;
  taskHandle_ = nullptr;
  ESP_LOGI(TAG, "USB Host stopped");
  vTaskDelete(nullptr);
}

void EspUsbHost::clientTaskLoop()
{
  while (running_)
  {
    esp_err_t err = usb_host_client_handle_events(clientHandle_, portMAX_DELAY);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
    {
      ESP_LOGW(TAG, "usb_host_client_handle_events() failed: %s", esp_err_to_name(err));
      setLastError(err);
    }
  }
  clientTaskHandle_ = nullptr;
  vTaskDelete(nullptr);
}

void EspUsbHost::clientEventCallback(const usb_host_client_event_msg_t *eventMsg, void *arg)
{
  static_cast<EspUsbHost *>(arg)->handleClientEvent(eventMsg);
}

void EspUsbHost::handleClientEvent(const usb_host_client_event_msg_t *eventMsg)
{
  switch (eventMsg->event)
  {
  case USB_HOST_CLIENT_EVENT_NEW_DEV:
    handleNewDevice(eventMsg->new_dev.address);
    break;
  case USB_HOST_CLIENT_EVENT_DEV_GONE:
    handleDeviceGone(eventMsg->dev_gone.dev_hdl);
    break;
  default:
    ESP_LOGD(TAG, "Unhandled client event: %d", eventMsg->event);
    break;
  }
}

void EspUsbHost::handleNewDevice(uint8_t address)
{
  ESP_LOGI(TAG, "Device connected: address=%u", address);
  if (deviceHandle_)
  {
    ESP_LOGI(TAG, "Ignoring device at address=%u because address=%u is already open",
             address,
             deviceInfo_.address);
    return;
  }

  hasKeyboardInterface_ = false;
  keyboardInterfaceNumber_ = 0;
  hasVendorInterface_ = false;
  vendorInterfaceNumber_ = 0;
  hasVendorOutEndpoint_ = false;
  vendorOutEndpointAddress_ = 0;
  vendorOutPacketSize_ = 0;
  hasCdcControlInterface_ = false;
  hasCdcDataInterface_ = false;
  cdcConfigured_ = false;
  cdcControlInterfaceNumber_ = 0;
  cdcDataInterfaceNumber_ = 0;
  hasSerialOutEndpoint_ = false;
  serialOutEndpointAddress_ = 0;
  serialOutPacketSize_ = 0;
  hasVendorSerialInterface_ = false;
  vendorSerialSupported_ = false;
  vendorSerialInterfaceNumber_ = 0;

  esp_err_t err = usb_host_device_open(clientHandle_, address, &deviceHandle_);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "usb_host_device_open() failed: %s", esp_err_to_name(err));
    setLastError(err);
    return;
  }

  usb_device_info_t devInfo = {};
  err = usb_host_device_info(deviceHandle_, &devInfo);
  if (err == ESP_OK)
  {
    manufacturer_ = usbString(devInfo.str_desc_manufacturer);
    product_ = usbString(devInfo.str_desc_product);
    serial_ = usbString(devInfo.str_desc_serial_num);
  }

  const usb_device_desc_t *devDesc = nullptr;
  err = usb_host_get_device_descriptor(deviceHandle_, &devDesc);
  if (err == ESP_OK && devDesc)
  {
    deviceInfo_.address = address;
    deviceInfo_.vid = devDesc->idVendor;
    deviceInfo_.pid = devDesc->idProduct;
    deviceInfo_.manufacturer = manufacturer_.c_str();
    deviceInfo_.product = product_.c_str();
    deviceInfo_.serial = serial_.c_str();
    ESP_LOGI(TAG, "VID=%04x PID=%04x manufacturer=\"%s\" product=\"%s\" serial=\"%s\"",
             deviceInfo_.vid, deviceInfo_.pid, deviceInfo_.manufacturer, deviceInfo_.product, deviceInfo_.serial);
    vendorSerialSupported_ = isKnownVendorSerial(deviceInfo_.vid, deviceInfo_.pid);
    if (vendorSerialSupported_)
    {
      ESP_LOGI(TAG, "%s VCP candidate detected: VID=%04x PID=%04x",
               vendorSerialName(deviceInfo_.vid),
               deviceInfo_.vid,
               deviceInfo_.pid);
    }
  }

  const usb_config_desc_t *configDesc = nullptr;
  err = usb_host_get_active_config_descriptor(deviceHandle_, &configDesc);
  if (err != ESP_OK || !configDesc)
  {
    ESP_LOGE(TAG, "usb_host_get_active_config_descriptor() failed: %s", esp_err_to_name(err));
    setLastError(err);
    return;
  }

  const bool isHub = devDesc && (devDesc->bDeviceClass == USB_CLASS_HUB_VALUE || configHasInterfaceClass(configDesc, USB_CLASS_HUB_VALUE));
  const bool hasHid = configHasInterfaceClass(configDesc, USB_CLASS_HID_VALUE);
  const bool hasCdc = configHasInterfaceClass(configDesc, USB_CLASS_CDC_CONTROL_VALUE) || configHasInterfaceClass(configDesc, USB_CLASS_CDC_DATA_VALUE);
  if (isHub || (!hasHid && !hasCdc && !vendorSerialSupported_))
  {
    ESP_LOGI(TAG, "Ignoring %s device at address=%u", isHub ? "hub" : "unsupported", address);
    usb_host_device_close(clientHandle_, deviceHandle_);
    deviceHandle_ = nullptr;
    return;
  }

  if (deviceConnectedCallback_)
  {
    deviceConnectedCallback_(deviceInfo_);
  }

  parseConfigDescriptor(configDesc);
}

void EspUsbHost::handleDeviceGone(usb_device_handle_t goneHandle)
{
  ESP_LOGI(TAG, "Device disconnected");
  if (deviceHandle_ && goneHandle && goneHandle != deviceHandle_)
  {
    ESP_LOGI(TAG, "Ignoring disconnect for a device that is not currently open");
    return;
  }

  releaseEndpoints(false);
  releaseInterfaces();
  if (deviceHandle_)
  {
    usb_host_device_close(clientHandle_, deviceHandle_);
    deviceHandle_ = nullptr;
  }
  else if (goneHandle)
  {
    usb_host_device_close(clientHandle_, goneHandle);
  }

  if (deviceDisconnectedCallback_)
  {
    deviceDisconnectedCallback_(deviceInfo_);
  }
  deviceInfo_ = EspUsbHostDeviceInfo();
  hasKeyboardInterface_ = false;
  keyboardInterfaceNumber_ = 0;
  hasVendorInterface_ = false;
  vendorInterfaceNumber_ = 0;
  hasVendorOutEndpoint_ = false;
  vendorOutEndpointAddress_ = 0;
  vendorOutPacketSize_ = 0;
  hasCdcControlInterface_ = false;
  hasCdcDataInterface_ = false;
  cdcConfigured_ = false;
  cdcControlInterfaceNumber_ = 0;
  cdcDataInterfaceNumber_ = 0;
  hasSerialOutEndpoint_ = false;
  serialOutEndpointAddress_ = 0;
  serialOutPacketSize_ = 0;
  hasVendorSerialInterface_ = false;
  vendorSerialSupported_ = false;
  vendorSerialInterfaceNumber_ = 0;
}

void EspUsbHost::parseConfigDescriptor(const usb_config_desc_t *configDesc)
{
  ESP_LOGI(TAG, "Configuration descriptor: totalLength=%u interfaces=%u value=%u attributes=0x%02x maxPower=%umA",
           configDesc->wTotalLength,
           configDesc->bNumInterfaces,
           configDesc->bConfigurationValue,
           configDesc->bmAttributes,
           configDesc->bMaxPower * 2);

  const uint8_t *p = reinterpret_cast<const uint8_t *>(configDesc);
  for (int i = 0; i < configDesc->wTotalLength;)
  {
    const uint8_t length = p[i];
    if (length < 2 || i + length > configDesc->wTotalLength)
    {
      ESP_LOGW(TAG, "Invalid descriptor length=%u offset=%d", length, i);
      return;
    }
    handleDescriptor(p[i + 1], &p[i]);
    i += length;
  }
}

void EspUsbHost::handleDescriptor(uint8_t descriptorType, const uint8_t *data)
{
  switch (descriptorType)
  {
  case USB_INTERFACE_DESC:
  {
    const usb_intf_desc_t *intf = reinterpret_cast<const usb_intf_desc_t *>(data);
    currentInterfaceNumber_ = intf->bInterfaceNumber;
    currentInterfaceClass_ = intf->bInterfaceClass;
    currentInterfaceSubClass_ = intf->bInterfaceSubClass;
    currentInterfaceProtocol_ = intf->bInterfaceProtocol;
    currentClaimResult_ = ESP_OK;

    ESP_LOGI(TAG, "Interface %u class=0x%02x subclass=0x%02x protocol=0x%02x endpoints=%u",
             currentInterfaceNumber_, currentInterfaceClass_, currentInterfaceSubClass_,
             currentInterfaceProtocol_, intf->bNumEndpoints);

    const bool isVendorSerialInterface = vendorSerialSupported_ &&
                                         currentInterfaceClass_ == USB_CLASS_VENDOR_VALUE &&
                                         !hasVendorSerialInterface_;
    if (currentInterfaceClass_ == USB_CLASS_HID_VALUE ||
        currentInterfaceClass_ == USB_CLASS_CDC_CONTROL_VALUE ||
        currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE ||
        isVendorSerialInterface)
    {
      currentClaimResult_ = usb_host_interface_claim(clientHandle_, deviceHandle_, currentInterfaceNumber_, intf->bAlternateSetting);
      if (currentClaimResult_ == ESP_OK && interfaceCount_ < sizeof(interfaces_))
      {
        interfaces_[interfaceCount_++] = currentInterfaceNumber_;
        ESP_LOGI(TAG, "Interface %u claimed", currentInterfaceNumber_);
        if (currentInterfaceSubClass_ == HID_SUBCLASS_BOOT_VALUE &&
            currentInterfaceProtocol_ == HID_PROTOCOL_KEYBOARD_VALUE &&
            !hasKeyboardInterface_)
        {
          hasKeyboardInterface_ = true;
          keyboardInterfaceNumber_ = currentInterfaceNumber_;
          ESP_LOGI(TAG, "Keyboard interface ready: iface=%u", keyboardInterfaceNumber_);
        }
        if (currentInterfaceClass_ == USB_CLASS_CDC_CONTROL_VALUE)
        {
          hasCdcControlInterface_ = true;
          cdcControlInterfaceNumber_ = currentInterfaceNumber_;
          ESP_LOGI(TAG, "CDC control interface ready: iface=%u", cdcControlInterfaceNumber_);
          configureCdcAcm();
        }
        else if (currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE)
        {
          hasCdcDataInterface_ = true;
          cdcDataInterfaceNumber_ = currentInterfaceNumber_;
          ESP_LOGI(TAG, "CDC data interface ready: iface=%u", cdcDataInterfaceNumber_);
        }
        else if (isVendorSerialInterface)
        {
          hasVendorSerialInterface_ = true;
          vendorSerialInterfaceNumber_ = currentInterfaceNumber_;
          ESP_LOGI(TAG, "%s VCP interface ready: iface=%u",
                   vendorSerialName(deviceInfo_.vid),
                   vendorSerialInterfaceNumber_);
          configureVendorSerial();
        }
      }
      else
      {
        ESP_LOGW(TAG, "usb_host_interface_claim(%u) failed: %s", currentInterfaceNumber_, esp_err_to_name(currentClaimResult_));
        setLastError(currentClaimResult_);
      }
    }
    break;
  }

  case USB_ENDPOINT_DESC:
  {
    const usb_ep_desc_t *ep = reinterpret_cast<const usb_ep_desc_t *>(data);
    const bool isIn = (ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0;
    const bool isInterrupt = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_INT;
    const bool isBulk = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_BULK;

    const bool isSerialBulkEndpoint = currentClaimResult_ == ESP_OK &&
                                      (currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE ||
                                       (vendorSerialSupported_ && currentInterfaceClass_ == USB_CLASS_VENDOR_VALUE)) &&
                                      isBulk;
    if (isSerialBulkEndpoint)
    {
      if (!isIn)
      {
        hasSerialOutEndpoint_ = true;
        serialOutEndpointAddress_ = ep->bEndpointAddress;
        serialOutPacketSize_ = ep->wMaxPacketSize;
        ESP_LOGI(TAG, "%s bulk OUT endpoint ready: iface=%u ep=0x%02x size=%u",
                 currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE ? "CDC" : vendorSerialName(deviceInfo_.vid),
                 currentInterfaceNumber_,
                 ep->bEndpointAddress,
                 ep->wMaxPacketSize);
        return;
      }

      EndpointState *endpoint = allocateEndpoint();
      if (!endpoint)
      {
        ESP_LOGW(TAG, "No endpoint slots available");
        setLastError(ESP_ERR_NO_MEM);
        return;
      }

      esp_err_t err = usb_host_transfer_alloc(ep->wMaxPacketSize, 0, &endpoint->transfer);
      if (err != ESP_OK)
      {
        endpoint->inUse = false;
        ESP_LOGW(TAG, "usb_host_transfer_alloc(serial IN) failed: %s", esp_err_to_name(err));
        setLastError(err);
        return;
      }

      endpoint->address = ep->bEndpointAddress;
      endpoint->interfaceNumber = currentInterfaceNumber_;
      endpoint->interfaceClass = currentInterfaceClass_;
      endpoint->interfaceSubClass = currentInterfaceSubClass_;
      endpoint->interfaceProtocol = currentInterfaceProtocol_;
      endpoint->transfer->device_handle = deviceHandle_;
      endpoint->transfer->bEndpointAddress = ep->bEndpointAddress;
      endpoint->transfer->callback = transferCallback;
      endpoint->transfer->context = this;
      endpoint->transfer->num_bytes = ep->wMaxPacketSize;

      err = usb_host_transfer_submit(endpoint->transfer);
      if (err != ESP_OK)
      {
        ESP_LOGW(TAG, "usb_host_transfer_submit(serial IN) failed: %s", esp_err_to_name(err));
        setLastError(err);
      }
      else
      {
        ESP_LOGI(TAG, "%s bulk IN endpoint ready: iface=%u ep=0x%02x size=%u",
                 currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE ? "CDC" : vendorSerialName(deviceInfo_.vid),
                 endpoint->interfaceNumber,
                 endpoint->address,
                 ep->wMaxPacketSize);
      }
      return;
    }

    if (currentClaimResult_ == ESP_OK &&
        currentInterfaceClass_ == USB_CLASS_HID_VALUE &&
        !isIn &&
        isInterrupt)
    {
      hasVendorOutEndpoint_ = true;
      vendorInterfaceNumber_ = currentInterfaceNumber_;
      vendorOutEndpointAddress_ = ep->bEndpointAddress;
      vendorOutPacketSize_ = ep->wMaxPacketSize;
      ESP_LOGI(TAG, "HID interrupt OUT endpoint ready: iface=%u ep=0x%02x size=%u interval=%u",
               currentInterfaceNumber_, ep->bEndpointAddress, ep->wMaxPacketSize, ep->bInterval);
      return;
    }

    if (currentClaimResult_ != ESP_OK || currentInterfaceClass_ != USB_CLASS_HID_VALUE || !isIn || !isInterrupt)
    {
      ESP_LOGD(TAG, "Skipping endpoint 0x%02x iface=%u class=0x%02x attrs=0x%02x",
               ep->bEndpointAddress,
               currentInterfaceNumber_,
               currentInterfaceClass_,
               ep->bmAttributes);
      return;
    }

    EndpointState *endpoint = allocateEndpoint();
    if (!endpoint)
    {
      ESP_LOGW(TAG, "No endpoint slots available");
      setLastError(ESP_ERR_NO_MEM);
      return;
    }

    esp_err_t err = usb_host_transfer_alloc(ep->wMaxPacketSize, 0, &endpoint->transfer);
    if (err != ESP_OK)
    {
      endpoint->inUse = false;
      ESP_LOGW(TAG, "usb_host_transfer_alloc() failed: %s", esp_err_to_name(err));
      setLastError(err);
      return;
    }

    endpoint->address = ep->bEndpointAddress;
    endpoint->interfaceNumber = currentInterfaceNumber_;
    endpoint->interfaceClass = currentInterfaceClass_;
    endpoint->interfaceSubClass = currentInterfaceSubClass_;
    endpoint->interfaceProtocol = currentInterfaceProtocol_;
    endpoint->transfer->device_handle = deviceHandle_;
    endpoint->transfer->bEndpointAddress = ep->bEndpointAddress;
    endpoint->transfer->callback = transferCallback;
    endpoint->transfer->context = this;
    endpoint->transfer->num_bytes = ep->wMaxPacketSize;

    err = usb_host_transfer_submit(endpoint->transfer);
    if (err != ESP_OK)
    {
      ESP_LOGW(TAG, "usb_host_transfer_submit() failed: %s", esp_err_to_name(err));
      setLastError(err);
    }
    else
    {
      ESP_LOGI(TAG, "HID interrupt IN endpoint ready: iface=%u ep=0x%02x size=%u interval=%u",
               endpoint->interfaceNumber, endpoint->address, ep->wMaxPacketSize, ep->bInterval);
    }
    break;
  }

  case USB_HID_DESC:
    ESP_LOGD(TAG, "HID descriptor");
    break;

  default:
    break;
  }
}

void EspUsbHost::transferCallback(usb_transfer_t *transfer)
{
  static_cast<EspUsbHost *>(transfer->context)->handleTransfer(transfer);
}

void EspUsbHost::controlTransferCallback(usb_transfer_t *transfer)
{
  EspUsbHost *host = static_cast<EspUsbHost *>(transfer->context);
  if (transfer->status != USB_TRANSFER_STATUS_COMPLETED)
  {
    ESP_LOGD(TAG, "control transfer status=%d", transfer->status);
    if (host)
    {
      host->setLastError(ESP_FAIL);
    }
  }
  usb_host_transfer_free(transfer);
}

void EspUsbHost::outputTransferCallback(usb_transfer_t *transfer)
{
  EspUsbHost *host = static_cast<EspUsbHost *>(transfer->context);
  if (transfer->status != USB_TRANSFER_STATUS_COMPLETED)
  {
    ESP_LOGD(TAG, "output transfer status=%d ep=0x%02x", transfer->status, transfer->bEndpointAddress);
    if (host)
    {
      host->setLastError(ESP_FAIL);
    }
  }
  usb_host_transfer_free(transfer);
}

void EspUsbHost::serialOutTransferCallback(usb_transfer_t *transfer)
{
  EspUsbHost *host = static_cast<EspUsbHost *>(transfer->context);
  if (transfer->status != USB_TRANSFER_STATUS_COMPLETED)
  {
    ESP_LOGD(TAG, "serial OUT transfer status=%d ep=0x%02x", transfer->status, transfer->bEndpointAddress);
    if (host)
    {
      host->setLastError(ESP_FAIL);
    }
  }
  usb_host_transfer_free(transfer);
}

void EspUsbHost::handleTransfer(usb_transfer_t *transfer)
{
  EndpointState *endpoint = findEndpoint(transfer->bEndpointAddress);
  if (!endpoint)
  {
    return;
  }

  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes > 0)
  {
    const char *transferKind = "input";
    if (endpoint->interfaceClass == USB_CLASS_HID_VALUE)
    {
      transferKind = "HID input";
    }
    else if (endpoint->interfaceClass == USB_CLASS_CDC_DATA_VALUE)
    {
      transferKind = "CDC serial input";
    }
    else if (vendorSerialSupported_ && endpoint->interfaceClass == USB_CLASS_VENDOR_VALUE)
    {
      transferKind = "VCP serial input";
    }
    ESP_LOGV(TAG, "%s iface=%u ep=0x%02x len=%u",
             transferKind,
             endpoint->interfaceNumber,
             transfer->bEndpointAddress,
             transfer->actual_num_bytes);

    EspUsbHostHIDInput input;
    input.address = deviceInfo_.address;
    input.interfaceNumber = endpoint->interfaceNumber;
    input.subclass = endpoint->interfaceSubClass;
    input.protocol = endpoint->interfaceProtocol;
    input.data = transfer->data_buffer;
    input.length = transfer->actual_num_bytes;
    if (hidInputCallback_)
    {
      hidInputCallback_(input);
    }

    if (endpoint->interfaceClass == USB_CLASS_HID_VALUE)
    {
      if (transfer->actual_num_bytes >= 5 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_MOUSE)
      {
        handleMouse(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
      }
      else if (transfer->actual_num_bytes >= 9 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_KEYBOARD)
      {
        handleKeyboard(*endpoint, transfer->data_buffer + 1, transfer->actual_num_bytes - 1);
      }
      else if (transfer->actual_num_bytes >= 3 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_CONSUMER_CONTROL)
      {
        handleConsumerControl(*endpoint, transfer->data_buffer + 1, transfer->actual_num_bytes - 1);
      }
      else if (transfer->actual_num_bytes >= 2 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_SYSTEM_CONTROL)
      {
        handleSystemControl(*endpoint, transfer->data_buffer + 1, transfer->actual_num_bytes - 1);
      }
      else if (transfer->actual_num_bytes >= 12 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_GAMEPAD)
      {
        handleGamepad(*endpoint, transfer->data_buffer + 1, transfer->actual_num_bytes - 1);
      }
      else if (transfer->actual_num_bytes >= 2 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_VENDOR)
      {
        handleVendorInput(*endpoint, transfer->data_buffer + 1, transfer->actual_num_bytes - 1);
      }
      else if (endpoint->interfaceSubClass == HID_SUBCLASS_BOOT_VALUE &&
               endpoint->interfaceProtocol == HID_PROTOCOL_KEYBOARD_VALUE)
      {
        handleKeyboard(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
      }
      else if (endpoint->interfaceSubClass == HID_SUBCLASS_BOOT_VALUE &&
               endpoint->interfaceProtocol == HID_PROTOCOL_MOUSE_VALUE)
      {
        handleMouse(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
      }
    }
    else if (endpoint->interfaceClass == USB_CLASS_CDC_DATA_VALUE ||
             (vendorSerialSupported_ && endpoint->interfaceClass == USB_CLASS_VENDOR_VALUE))
    {
      handleSerial(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
    }
  }
  else if (transfer->status != USB_TRANSFER_STATUS_COMPLETED)
  {
    ESP_LOGD(TAG, "transfer status=%d ep=0x%02x", transfer->status, transfer->bEndpointAddress);
  }

  if (transfer->status == USB_TRANSFER_STATUS_NO_DEVICE ||
      transfer->status == USB_TRANSFER_STATUS_CANCELED)
  {
    return;
  }

  if (running_ && deviceHandle_)
  {
    esp_err_t err = usb_host_transfer_submit(transfer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE && err != ESP_ERR_NOT_FINISHED)
    {
      ESP_LOGD(TAG, "usb_host_transfer_submit() failed: %s", esp_err_to_name(err));
      setLastError(err);
    }
  }
}

void EspUsbHost::handleKeyboard(EndpointState &endpoint, const uint8_t *data, size_t length)
{
  if (length < ESP_USB_HOST_BOOT_KEYBOARD_REPORT_SIZE)
  {
    return;
  }

  if (!espUsbHostIsBootKeyboardReportValid(data, length))
  {
    ESP_LOGD(TAG, "Ignoring invalid boot keyboard report: %02x %02x %02x %02x %02x %02x %02x %02x",
             data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
    return;
  }

  EspUsbHostKeyboardReport previousReport;
  EspUsbHostKeyboardReport currentReport;
  memcpy(previousReport.data, endpoint.lastKeyboardReport, ESP_USB_HOST_BOOT_KEYBOARD_REPORT_SIZE);
  memcpy(currentReport.data, data, ESP_USB_HOST_BOOT_KEYBOARD_REPORT_SIZE);

  EspUsbHostKeyboardEvent events[ESP_USB_HOST_BOOT_KEYBOARD_MAX_EVENTS];
  const size_t eventCount = espUsbHostBuildKeyboardEvents(endpoint.interfaceNumber,
                                                          previousReport,
                                                          currentReport,
                                                          keyboardLayout_,
                                                          events,
                                                          ESP_USB_HOST_BOOT_KEYBOARD_MAX_EVENTS);

  if (!endpoint.keyboardReportReady)
  {
    endpoint.keyboardReportReady = true;
  }

  for (size_t i = 0; i < eventCount; i++)
  {
    ESP_LOGD(TAG, "Keyboard %s iface=%u keycode=0x%02x ascii=0x%02x modifiers=0x%02x",
             events[i].pressed ? "press" : "release",
             events[i].interfaceNumber,
             events[i].keycode,
             events[i].ascii,
             events[i].modifiers);
    if (keyboardCallback_)
    {
      keyboardCallback_(events[i]);
    }
  }

  memcpy(endpoint.lastKeyboardReport, data, ESP_USB_HOST_BOOT_KEYBOARD_REPORT_SIZE);
}

void EspUsbHost::handleMouse(EndpointState &endpoint, const uint8_t *data, size_t length)
{
  if (!mouseCallback_)
  {
    return;
  }

  if (endpoint.interfaceProtocol != HID_PROTOCOL_MOUSE_VALUE &&
      length >= 5 &&
      data[0] == ESP_USB_HOST_HID_REPORT_ID_MOUSE)
  {
    data += 1;
    length -= 1;
  }

  EspUsbHostMouseEvent event;
  if (!espUsbHostParseBootMouseReport(endpoint.interfaceNumber,
                                      data,
                                      length,
                                      endpoint.lastMouseButtons,
                                      event))
  {
    return;
  }

  ESP_LOGD(TAG, "Mouse iface=%u x=%d y=%d wheel=%d buttons=0x%02x previous=0x%02x",
           event.interfaceNumber,
           event.x,
           event.y,
           event.wheel,
           event.buttons,
           event.previousButtons);
  mouseCallback_(event);
  endpoint.lastMouseButtons = event.buttons;
}

void EspUsbHost::handleSerial(EndpointState &endpoint, const uint8_t *data, size_t length)
{
  if (!data || length == 0)
  {
    return;
  }

  if (cdcSerial_)
  {
    cdcSerial_->pushData(data, length);
  }

  if (!serialDataCallback_)
  {
    return;
  }

  EspUsbHostSerialData serial;
  serial.address = deviceInfo_.address;
  serial.interfaceNumber = endpoint.interfaceNumber;
  serial.data = data;
  serial.length = length;
  serialDataCallback_(serial);
}

void EspUsbHost::handleConsumerControl(EndpointState &endpoint, const uint8_t *data, size_t length)
{
  if (!consumerControlCallback_)
  {
    return;
  }

  EspUsbHostConsumerControlEvent event;
  if (!espUsbHostParseConsumerControlReport(endpoint.interfaceNumber,
                                            data,
                                            length,
                                            endpoint.lastConsumerUsage,
                                            event))
  {
    return;
  }

  ESP_LOGD(TAG, "ConsumerControl iface=%u usage=0x%04x pressed=%u released=%u",
           event.interfaceNumber,
           event.usage,
           event.pressed ? 1 : 0,
           event.released ? 1 : 0);
  consumerControlCallback_(event);
  endpoint.lastConsumerUsage = event.pressed ? event.usage : 0;
}

void EspUsbHost::handleGamepad(EndpointState &endpoint, const uint8_t *data, size_t length)
{
  if (!gamepadCallback_)
  {
    return;
  }

  EspUsbHostGamepadEvent event;
  if (!espUsbHostParseGamepadReport(endpoint.interfaceNumber,
                                    data,
                                    length,
                                    endpoint.lastGamepadButtons,
                                    event))
  {
    return;
  }

  ESP_LOGD(TAG, "Gamepad iface=%u x=%d y=%d z=%d rz=%d rx=%d ry=%d hat=%u buttons=0x%08lx previous=0x%08lx",
           event.interfaceNumber,
           event.x,
           event.y,
           event.z,
           event.rz,
           event.rx,
           event.ry,
           event.hat,
           static_cast<unsigned long>(event.buttons),
           static_cast<unsigned long>(event.previousButtons));
  gamepadCallback_(event);
  endpoint.lastGamepadButtons = event.buttons;
}

void EspUsbHost::handleVendorInput(EndpointState &endpoint, const uint8_t *data, size_t length)
{
  if (!hasVendorInterface_)
  {
    hasVendorInterface_ = true;
    vendorInterfaceNumber_ = endpoint.interfaceNumber;
    ESP_LOGI(TAG, "Vendor HID interface ready: iface=%u", vendorInterfaceNumber_);
  }

  if (!vendorInputCallback_)
  {
    return;
  }

  EspUsbHostVendorInput input;
  input.interfaceNumber = endpoint.interfaceNumber;
  input.data = data;
  input.length = length;

  ESP_LOGD(TAG, "VendorInput iface=%u length=%u",
           input.interfaceNumber,
           static_cast<unsigned>(input.length));
  vendorInputCallback_(input);
}

void EspUsbHost::handleSystemControl(EndpointState &endpoint, const uint8_t *data, size_t length)
{
  if (!systemControlCallback_)
  {
    return;
  }

  EspUsbHostSystemControlEvent event;
  if (!espUsbHostParseSystemControlReport(endpoint.interfaceNumber,
                                          data,
                                          length,
                                          endpoint.lastSystemUsage,
                                          event))
  {
    return;
  }

  ESP_LOGD(TAG, "SystemControl iface=%u usage=0x%02x pressed=%u released=%u",
           event.interfaceNumber,
           event.usage,
           event.pressed ? 1 : 0,
           event.released ? 1 : 0);
  systemControlCallback_(event);
  endpoint.lastSystemUsage = event.pressed ? event.usage : 0;
}

EspUsbHost::EndpointState *EspUsbHost::findEndpoint(uint8_t endpointAddress)
{
  for (EndpointState &endpoint : endpoints_)
  {
    if (endpoint.inUse && endpoint.address == endpointAddress)
    {
      return &endpoint;
    }
  }
  return nullptr;
}

EspUsbHost::EndpointState *EspUsbHost::allocateEndpoint()
{
  for (EndpointState &endpoint : endpoints_)
  {
    if (!endpoint.inUse)
    {
      endpoint = EndpointState();
      endpoint.inUse = true;
      return &endpoint;
    }
  }
  return nullptr;
}

void EspUsbHost::releaseEndpoints(bool clearEndpoints)
{
  for (EndpointState &endpoint : endpoints_)
  {
    if (!endpoint.inUse)
    {
      continue;
    }
    if (endpoint.transfer)
    {
      if (clearEndpoints && deviceHandle_)
      {
        esp_err_t err = usb_host_endpoint_clear(deviceHandle_, endpoint.address);
        if (err != ESP_OK)
        {
          ESP_LOGD(TAG, "usb_host_endpoint_clear(0x%02x) failed: %s",
                   endpoint.address,
                   esp_err_to_name(err));
        }
      }
      usb_host_transfer_free(endpoint.transfer);
    }
    endpoint = EndpointState();
  }
}

void EspUsbHost::releaseInterfaces()
{
  for (uint8_t i = 0; i < interfaceCount_; i++)
  {
    usb_host_interface_release(clientHandle_, deviceHandle_, interfaces_[i]);
    interfaces_[i] = 0;
  }
  interfaceCount_ = 0;
}

void EspUsbHost::configureCdcAcm()
{
  if (cdcConfigured_ || !hasCdcControlInterface_ || !deviceHandle_ || !clientHandle_)
  {
    return;
  }

  uint8_t lineCoding[7] = {
      static_cast<uint8_t>(serialBaudRate_ & 0xff),
      static_cast<uint8_t>((serialBaudRate_ >> 8) & 0xff),
      static_cast<uint8_t>((serialBaudRate_ >> 16) & 0xff),
      static_cast<uint8_t>((serialBaudRate_ >> 24) & 0xff),
      0x00,
      0x00,
      0x08};

  usb_transfer_t *lineCodingTransfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + sizeof(lineCoding), 0, &lineCodingTransfer);
  if (err == ESP_OK)
  {
    usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(lineCodingTransfer->data_buffer);
    setup->bmRequestType = CDC_SET_REQUEST_TYPE;
    setup->bRequest = CDC_CLASS_REQUEST_SET_LINE_CODING;
    setup->wValue = 0;
    setup->wIndex = cdcControlInterfaceNumber_;
    setup->wLength = sizeof(lineCoding);
    memcpy(lineCodingTransfer->data_buffer + USB_SETUP_PACKET_SIZE, lineCoding, sizeof(lineCoding));
    lineCodingTransfer->device_handle = deviceHandle_;
    lineCodingTransfer->bEndpointAddress = 0;
    lineCodingTransfer->callback = controlTransferCallback;
    lineCodingTransfer->context = this;
    lineCodingTransfer->num_bytes = USB_SETUP_PACKET_SIZE + sizeof(lineCoding);
    err = usb_host_transfer_submit_control(clientHandle_, lineCodingTransfer);
    if (err != ESP_OK)
    {
      usb_host_transfer_free(lineCodingTransfer);
      setLastError(err);
    }
  }

  usb_transfer_t *lineStateTransfer = nullptr;
  err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE, 0, &lineStateTransfer);
  if (err == ESP_OK)
  {
    usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(lineStateTransfer->data_buffer);
    setup->bmRequestType = CDC_SET_REQUEST_TYPE;
    setup->bRequest = CDC_CLASS_REQUEST_SET_CONTROL_LINE_STATE;
    setup->wValue = (serialDtr_ ? 0x0001 : 0) | (serialRts_ ? 0x0002 : 0);
    setup->wIndex = cdcControlInterfaceNumber_;
    setup->wLength = 0;
    lineStateTransfer->device_handle = deviceHandle_;
    lineStateTransfer->bEndpointAddress = 0;
    lineStateTransfer->callback = controlTransferCallback;
    lineStateTransfer->context = this;
    lineStateTransfer->num_bytes = USB_SETUP_PACKET_SIZE;
    err = usb_host_transfer_submit_control(clientHandle_, lineStateTransfer);
    if (err != ESP_OK)
    {
      usb_host_transfer_free(lineStateTransfer);
      setLastError(err);
    }
  }

  cdcConfigured_ = true;
  ESP_LOGI(TAG, "CDC ACM configured: baud=%lu dtr=%u rts=%u",
           static_cast<unsigned long>(serialBaudRate_),
           serialDtr_ ? 1 : 0,
           serialRts_ ? 1 : 0);
}

void EspUsbHost::attachCdcSerial(EspUsbHostCdcSerial *serial)
{
  cdcSerial_ = serial;
}

void EspUsbHost::configureVendorSerial()
{
  if (!vendorSerialSupported_ || !hasVendorSerialInterface_ || !deviceHandle_ || !clientHandle_)
  {
    return;
  }

  ESP_LOGI(TAG, "Configuring %s VCP: iface=%u baud=%lu dtr=%u rts=%u",
           vendorSerialName(deviceInfo_.vid),
           vendorSerialInterfaceNumber_,
           static_cast<unsigned long>(serialBaudRate_),
           serialDtr_ ? 1 : 0,
           serialRts_ ? 1 : 0);

  if (deviceInfo_.vid == 0x0403)
  {
    uint16_t divisor = 0x001a;
    if (serialBaudRate_ == 9600)
    {
      divisor = 0x4138;
    }
    else if (serialBaudRate_ == 57600)
    {
      divisor = 0x0034;
    }
    else if (serialBaudRate_ == 230400)
    {
      divisor = 0x000d;
    }

    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x00, 0x0000, vendorSerialInterfaceNumber_);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x03, divisor, vendorSerialInterfaceNumber_);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x04, 0x0008, vendorSerialInterfaceNumber_);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x02, serialDtr_ ? 0x0011 : 0x0010, vendorSerialInterfaceNumber_);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x02, serialRts_ ? 0x0021 : 0x0020, vendorSerialInterfaceNumber_);
  }
  else if (deviceInfo_.vid == 0x10c4)
  {
    const uint8_t baud[4] = {
        static_cast<uint8_t>(serialBaudRate_ & 0xff),
        static_cast<uint8_t>((serialBaudRate_ >> 8) & 0xff),
        static_cast<uint8_t>((serialBaudRate_ >> 16) & 0xff),
        static_cast<uint8_t>((serialBaudRate_ >> 24) & 0xff)};
    submitVendorSerialControl(VENDOR_INTERFACE_OUT_REQUEST_TYPE, 0x00, 0x0001, vendorSerialInterfaceNumber_);
    submitVendorSerialControl(VENDOR_INTERFACE_OUT_REQUEST_TYPE, 0x1e, 0x0000, vendorSerialInterfaceNumber_, baud, sizeof(baud));
    submitVendorSerialControl(VENDOR_INTERFACE_OUT_REQUEST_TYPE, 0x03, 0x0800, vendorSerialInterfaceNumber_);
    submitVendorSerialControl(VENDOR_INTERFACE_OUT_REQUEST_TYPE, 0x07,
                              (serialDtr_ ? 0x0001 : 0) | (serialRts_ ? 0x0002 : 0) | 0x0300,
                              vendorSerialInterfaceNumber_);
  }
  else if (deviceInfo_.vid == 0x1a86)
  {
    uint16_t baudReg = 0xd980;
    if (serialBaudRate_ == 9600)
    {
      baudReg = 0xb282;
    }
    else if (serialBaudRate_ == 57600)
    {
      baudReg = 0x9881;
    }
    else if (serialBaudRate_ == 230400)
    {
      baudReg = 0xcc83;
    }

    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0xa1, 0x0000, 0x0000);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x9a, 0x1312, baudReg);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x9a, 0x2518, 0x00c3);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0xa4,
                              (serialDtr_ ? 0x0020 : 0) | (serialRts_ ? 0x0040 : 0),
                              vendorSerialInterfaceNumber_);
  }
}

bool EspUsbHost::submitVendorSerialControl(uint8_t requestType,
                                           uint8_t request,
                                           uint16_t value,
                                           uint16_t index,
                                           const uint8_t *data,
                                           size_t length)
{
  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + length, 0, &transfer);
  if (err != ESP_OK)
  {
    setLastError(err);
    return false;
  }

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = requestType;
  setup->bRequest = request;
  setup->wValue = value;
  setup->wIndex = index;
  setup->wLength = length;
  if (length > 0 && data)
  {
    memcpy(transfer->data_buffer + USB_SETUP_PACKET_SIZE, data, length);
  }

  transfer->device_handle = deviceHandle_;
  transfer->bEndpointAddress = 0;
  transfer->callback = controlTransferCallback;
  transfer->context = this;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE + length;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGD(TAG, "VCP control request 0x%02x failed: %s", request, esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }
  return true;
}

void EspUsbHost::setLastError(esp_err_t err)
{
  if (err != ESP_OK)
  {
    lastError_ = err;
  }
}

String EspUsbHost::usbString(const usb_str_desc_t *strDesc)
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

EspUsbHostCdcSerial::EspUsbHostCdcSerial(EspUsbHost &host) : host_(host)
{
}

bool EspUsbHostCdcSerial::begin(uint32_t baud)
{
  portENTER_CRITICAL(&rxMux_);
  rxHead_ = 0;
  rxTail_ = 0;
  portEXIT_CRITICAL(&rxMux_);

  host_.attachCdcSerial(this);
  return host_.setSerialBaudRate(baud);
}

void EspUsbHostCdcSerial::end()
{
  if (host_.cdcSerial_ == this)
  {
    host_.attachCdcSerial(nullptr);
  }
}

bool EspUsbHostCdcSerial::connected() const
{
  return host_.serialReady();
}

int EspUsbHostCdcSerial::available()
{
  portENTER_CRITICAL(&rxMux_);
  const size_t count = rxHead_ >= rxTail_ ? rxHead_ - rxTail_ : RX_BUFFER_SIZE - rxTail_ + rxHead_;
  portEXIT_CRITICAL(&rxMux_);
  return static_cast<int>(count);
}

int EspUsbHostCdcSerial::read()
{
  portENTER_CRITICAL(&rxMux_);
  if (rxHead_ == rxTail_)
  {
    portEXIT_CRITICAL(&rxMux_);
    return -1;
  }
  const uint8_t value = rxBuffer_[rxTail_];
  rxTail_ = nextIndex(rxTail_);
  portEXIT_CRITICAL(&rxMux_);
  return value;
}

int EspUsbHostCdcSerial::peek()
{
  portENTER_CRITICAL(&rxMux_);
  if (rxHead_ == rxTail_)
  {
    portEXIT_CRITICAL(&rxMux_);
    return -1;
  }
  const uint8_t value = rxBuffer_[rxTail_];
  portEXIT_CRITICAL(&rxMux_);
  return value;
}

void EspUsbHostCdcSerial::flush()
{
}

size_t EspUsbHostCdcSerial::write(uint8_t data)
{
  return write(&data, 1);
}

size_t EspUsbHostCdcSerial::write(const uint8_t *buffer, size_t size)
{
  if (size == 0)
  {
    return 0;
  }
  return host_.sendSerial(buffer, size) ? size : 0;
}

bool EspUsbHostCdcSerial::setBaudRate(uint32_t baud)
{
  return host_.setSerialBaudRate(baud);
}

bool EspUsbHostCdcSerial::setDtr(bool enable)
{
  host_.serialDtr_ = enable;
  if (host_.hasCdcControlInterface_)
  {
    host_.cdcConfigured_ = false;
    host_.configureCdcAcm();
  }
  else if (host_.vendorSerialSupported_)
  {
    host_.configureVendorSerial();
  }
  return true;
}

bool EspUsbHostCdcSerial::setRts(bool enable)
{
  host_.serialRts_ = enable;
  if (host_.hasCdcControlInterface_)
  {
    host_.cdcConfigured_ = false;
    host_.configureCdcAcm();
  }
  else if (host_.vendorSerialSupported_)
  {
    host_.configureVendorSerial();
  }
  return true;
}

void EspUsbHostCdcSerial::pushData(const uint8_t *data, size_t length)
{
  portENTER_CRITICAL(&rxMux_);
  for (size_t i = 0; i < length; i++)
  {
    const size_t next = nextIndex(rxHead_);
    if (next == rxTail_)
    {
      rxTail_ = nextIndex(rxTail_);
    }
    rxBuffer_[rxHead_] = data[i];
    rxHead_ = next;
  }
  portEXIT_CRITICAL(&rxMux_);
}

size_t EspUsbHostCdcSerial::nextIndex(size_t index) const
{
  index++;
  if (index >= RX_BUFFER_SIZE)
  {
    return 0;
  }
  return index;
}
