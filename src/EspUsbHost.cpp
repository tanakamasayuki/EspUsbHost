#include "EspUsbHost.h"
#include "EspUsbHostHid.h"

static const char *TAG __attribute__((unused)) = "EspUsbHost";

static constexpr uint8_t USB_CLASS_HID_VALUE = 0x03;
static constexpr uint8_t USB_CLASS_HUB_VALUE = 0x09;
static constexpr uint8_t HID_SUBCLASS_BOOT_VALUE = 0x01;
static constexpr uint8_t HID_PROTOCOL_KEYBOARD_VALUE = 0x01;
static constexpr uint8_t HID_PROTOCOL_MOUSE_VALUE = 0x02;
static constexpr uint8_t HID_CLASS_REQUEST_SET_REPORT = 0x09;
static constexpr uint8_t HID_SET_REPORT_REQUEST_TYPE = 0x21;

static bool configHasInterfaceClass(const usb_config_desc_t *configDesc, uint8_t interfaceClass) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(configDesc);
  for (int i = 0; i < configDesc->wTotalLength;) {
    const uint8_t length = p[i];
    if (length < 2 || i + length > configDesc->wTotalLength) {
      return false;
    }
    if (p[i + 1] == USB_INTERFACE_DESC) {
      const usb_intf_desc_t *intf = reinterpret_cast<const usb_intf_desc_t *>(&p[i]);
      if (intf->bInterfaceClass == interfaceClass) {
        return true;
      }
    }
    i += length;
  }
  return false;
}

EspUsbHost::EspUsbHost() = default;

EspUsbHost::~EspUsbHost() {
  end();
}

bool EspUsbHost::begin() {
  return begin(EspUsbHostConfig());
}

bool EspUsbHost::begin(const EspUsbHostConfig &config) {
  if (running_) {
    ESP_LOGW(TAG, "begin() called while USB Host is already running");
    return true;
  }

  config_ = config;
  running_ = true;
  ready_ = false;
  lastError_ = ESP_OK;

  BaseType_t created;
  if (config_.taskCore == tskNO_AFFINITY) {
    created = xTaskCreate(taskEntry, "EspUsbHost", config_.taskStackSize, this, config_.taskPriority, &taskHandle_);
  } else {
    created = xTaskCreatePinnedToCore(taskEntry, "EspUsbHost", config_.taskStackSize, this, config_.taskPriority, &taskHandle_, config_.taskCore);
  }

  if (created != pdPASS) {
    running_ = false;
    setLastError(ESP_ERR_NO_MEM);
    return false;
  }

  const uint32_t start = millis();
  while (running_ && !ready_ && millis() - start < 1000) {
    delay(1);
  }
  return ready_;
}

void EspUsbHost::end() {
  if (!running_) {
    return;
  }

  ESP_LOGI(TAG, "Stopping USB Host");
  running_ = false;
  if (clientTaskHandle_) {
    for (int i = 0; i < 200 && clientTaskHandle_; i++) {
      delay(1);
    }
    if (clientTaskHandle_) {
      vTaskDelete(clientTaskHandle_);
      clientTaskHandle_ = nullptr;
    }
  }
  if (taskHandle_) {
    for (int i = 0; i < 200 && taskHandle_; i++) {
      delay(1);
    }
    if (taskHandle_) {
      vTaskDelete(taskHandle_);
      taskHandle_ = nullptr;
    }
  }
  ready_ = false;
}

bool EspUsbHost::ready() const {
  return ready_;
}

void EspUsbHost::onDeviceConnected(DeviceCallback callback) {
  deviceConnectedCallback_ = callback;
}

void EspUsbHost::onDeviceDisconnected(DeviceCallback callback) {
  deviceDisconnectedCallback_ = callback;
}

void EspUsbHost::onKeyboard(KeyboardCallback callback) {
  keyboardCallback_ = callback;
}

void EspUsbHost::onMouse(MouseCallback callback) {
  mouseCallback_ = callback;
}

void EspUsbHost::onHIDInput(HIDInputCallback callback) {
  hidInputCallback_ = callback;
}

void EspUsbHost::onConsumerControl(ConsumerControlCallback callback) {
  consumerControlCallback_ = callback;
}

void EspUsbHost::onGamepad(GamepadCallback callback) {
  gamepadCallback_ = callback;
}

void EspUsbHost::onVendorInput(VendorInputCallback callback) {
  vendorInputCallback_ = callback;
}

void EspUsbHost::setKeyboardLayout(EspUsbHostKeyboardLayout layout) {
  keyboardLayout_ = layout;
}

bool EspUsbHost::sendHIDReport(uint8_t interfaceNumber,
                               uint8_t reportType,
                               uint8_t reportId,
                               const uint8_t *data,
                               size_t length) {
  if (!running_ || !deviceHandle_ || !clientHandle_) {
    ESP_LOGW(TAG, "sendHIDReport() called while no HID device is open");
    return false;
  }
  if (length > 0 && !data) {
    ESP_LOGW(TAG, "sendHIDReport() called with null data");
    return false;
  }
  if (reportType != ESP_USB_HOST_HID_REPORT_TYPE_OUTPUT &&
      reportType != ESP_USB_HOST_HID_REPORT_TYPE_FEATURE) {
    ESP_LOGW(TAG, "sendHIDReport() unsupported reportType=%u", reportType);
    return false;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + length, 0, &transfer);
  if (err != ESP_OK) {
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
  if (length > 0) {
    memcpy(transfer->data_buffer + USB_SETUP_PACKET_SIZE, data, length);
  }

  transfer->device_handle = deviceHandle_;
  transfer->bEndpointAddress = 0;
  transfer->callback = controlTransferCallback;
  transfer->context = this;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE + length;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK) {
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

bool EspUsbHost::setKeyboardLeds(bool numLock, bool capsLock, bool scrollLock) {
  if (!hasKeyboardInterface_) {
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

int EspUsbHost::lastError() const {
  return lastError_;
}

const char *EspUsbHost::lastErrorName() const {
  return esp_err_to_name(lastError_);
}

void EspUsbHost::taskEntry(void *arg) {
  static_cast<EspUsbHost *>(arg)->taskLoop();
}

void EspUsbHost::clientTaskEntry(void *arg) {
  static_cast<EspUsbHost *>(arg)->clientTaskLoop();
}

void EspUsbHost::taskLoop() {
  usb_host_config_t hostConfig = {};
  hostConfig.skip_phy_setup = false;
  hostConfig.intr_flags = ESP_INTR_FLAG_LOWMED;

  esp_err_t err = usb_host_install(&hostConfig);
  if (err != ESP_OK) {
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
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "usb_host_client_register() failed: %s", esp_err_to_name(err));
    setLastError(err);
    usb_host_uninstall();
    running_ = false;
    taskHandle_ = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  BaseType_t created;
  if (config_.taskCore == tskNO_AFFINITY) {
    created = xTaskCreate(clientTaskEntry, "EspUsbHostClient", config_.taskStackSize, this, config_.taskPriority, &clientTaskHandle_);
  } else {
    created = xTaskCreatePinnedToCore(clientTaskEntry, "EspUsbHostClient", config_.taskStackSize, this, config_.taskPriority, &clientTaskHandle_, config_.taskCore);
  }
  if (created != pdPASS) {
    ESP_LOGE(TAG, "Failed to create USB Host client task");
    setLastError(ESP_ERR_NO_MEM);
    running_ = false;
  }

  ready_ = running_;
  ESP_LOGI(TAG, "USB Host started stack=%lu priority=%u core=%d",
           static_cast<unsigned long>(config_.taskStackSize),
           static_cast<unsigned>(config_.taskPriority),
           static_cast<int>(config_.taskCore));

  while (running_) {
    uint32_t eventFlags = 0;
    err = usb_host_lib_handle_events(portMAX_DELAY, &eventFlags);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
      ESP_LOGW(TAG, "usb_host_lib_handle_events() failed: %s", esp_err_to_name(err));
      setLastError(err);
    }
  }

  if (clientTaskHandle_) {
    vTaskDelete(clientTaskHandle_);
    clientTaskHandle_ = nullptr;
  }
  releaseEndpoints(true);
  releaseInterfaces();
  if (deviceHandle_) {
    usb_host_device_close(clientHandle_, deviceHandle_);
    deviceHandle_ = nullptr;
  }
  if (clientHandle_) {
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

void EspUsbHost::clientTaskLoop() {
  while (running_) {
    esp_err_t err = usb_host_client_handle_events(clientHandle_, portMAX_DELAY);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
      ESP_LOGW(TAG, "usb_host_client_handle_events() failed: %s", esp_err_to_name(err));
      setLastError(err);
    }
  }
  clientTaskHandle_ = nullptr;
  vTaskDelete(nullptr);
}

void EspUsbHost::clientEventCallback(const usb_host_client_event_msg_t *eventMsg, void *arg) {
  static_cast<EspUsbHost *>(arg)->handleClientEvent(eventMsg);
}

void EspUsbHost::handleClientEvent(const usb_host_client_event_msg_t *eventMsg) {
  switch (eventMsg->event) {
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

void EspUsbHost::handleNewDevice(uint8_t address) {
  ESP_LOGI(TAG, "Device connected: address=%u", address);
  hasKeyboardInterface_ = false;
  keyboardInterfaceNumber_ = 0;

  esp_err_t err = usb_host_device_open(clientHandle_, address, &deviceHandle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "usb_host_device_open() failed: %s", esp_err_to_name(err));
    setLastError(err);
    return;
  }

  usb_device_info_t devInfo = {};
  err = usb_host_device_info(deviceHandle_, &devInfo);
  if (err == ESP_OK) {
    manufacturer_ = usbString(devInfo.str_desc_manufacturer);
    product_ = usbString(devInfo.str_desc_product);
    serial_ = usbString(devInfo.str_desc_serial_num);
  }

  const usb_device_desc_t *devDesc = nullptr;
  err = usb_host_get_device_descriptor(deviceHandle_, &devDesc);
  if (err == ESP_OK && devDesc) {
    deviceInfo_.address = address;
    deviceInfo_.vid = devDesc->idVendor;
    deviceInfo_.pid = devDesc->idProduct;
    deviceInfo_.manufacturer = manufacturer_.c_str();
    deviceInfo_.product = product_.c_str();
    deviceInfo_.serial = serial_.c_str();
    ESP_LOGI(TAG, "VID=%04x PID=%04x manufacturer=\"%s\" product=\"%s\" serial=\"%s\"",
             deviceInfo_.vid, deviceInfo_.pid, deviceInfo_.manufacturer, deviceInfo_.product, deviceInfo_.serial);
  }

  const usb_config_desc_t *configDesc = nullptr;
  err = usb_host_get_active_config_descriptor(deviceHandle_, &configDesc);
  if (err != ESP_OK || !configDesc) {
    ESP_LOGE(TAG, "usb_host_get_active_config_descriptor() failed: %s", esp_err_to_name(err));
    setLastError(err);
    return;
  }

  const bool isHub = devDesc && (devDesc->bDeviceClass == USB_CLASS_HUB_VALUE || configHasInterfaceClass(configDesc, USB_CLASS_HUB_VALUE));
  const bool hasHid = configHasInterfaceClass(configDesc, USB_CLASS_HID_VALUE);
  if (isHub || !hasHid) {
    ESP_LOGI(TAG, "Ignoring %s device at address=%u", isHub ? "hub" : "non-HID", address);
    usb_host_device_close(clientHandle_, deviceHandle_);
    deviceHandle_ = nullptr;
    return;
  }

  if (deviceConnectedCallback_) {
    deviceConnectedCallback_(deviceInfo_);
  }

  parseConfigDescriptor(configDesc);
}

void EspUsbHost::handleDeviceGone(usb_device_handle_t goneHandle) {
  ESP_LOGI(TAG, "Device disconnected");
  releaseEndpoints(false);
  releaseInterfaces();
  if (deviceHandle_) {
    usb_host_device_close(clientHandle_, deviceHandle_);
    deviceHandle_ = nullptr;
  } else if (goneHandle) {
    usb_host_device_close(clientHandle_, goneHandle);
  }

  if (deviceDisconnectedCallback_) {
    deviceDisconnectedCallback_(deviceInfo_);
  }
  deviceInfo_ = EspUsbHostDeviceInfo();
  hasKeyboardInterface_ = false;
  keyboardInterfaceNumber_ = 0;
}

void EspUsbHost::parseConfigDescriptor(const usb_config_desc_t *configDesc) {
  ESP_LOGI(TAG, "Configuration descriptor: totalLength=%u interfaces=%u value=%u attributes=0x%02x maxPower=%umA",
           configDesc->wTotalLength,
           configDesc->bNumInterfaces,
           configDesc->bConfigurationValue,
           configDesc->bmAttributes,
           configDesc->bMaxPower * 2);

  const uint8_t *p = reinterpret_cast<const uint8_t *>(configDesc);
  for (int i = 0; i < configDesc->wTotalLength;) {
    const uint8_t length = p[i];
    if (length < 2 || i + length > configDesc->wTotalLength) {
      ESP_LOGW(TAG, "Invalid descriptor length=%u offset=%d", length, i);
      return;
    }
    handleDescriptor(p[i + 1], &p[i]);
    i += length;
  }
}

void EspUsbHost::handleDescriptor(uint8_t descriptorType, const uint8_t *data) {
  switch (descriptorType) {
    case USB_INTERFACE_DESC: {
      const usb_intf_desc_t *intf = reinterpret_cast<const usb_intf_desc_t *>(data);
      currentInterfaceNumber_ = intf->bInterfaceNumber;
      currentInterfaceClass_ = intf->bInterfaceClass;
      currentInterfaceSubClass_ = intf->bInterfaceSubClass;
      currentInterfaceProtocol_ = intf->bInterfaceProtocol;
      currentClaimResult_ = ESP_OK;

      ESP_LOGI(TAG, "Interface %u class=0x%02x subclass=0x%02x protocol=0x%02x endpoints=%u",
               currentInterfaceNumber_, currentInterfaceClass_, currentInterfaceSubClass_,
               currentInterfaceProtocol_, intf->bNumEndpoints);

      if (currentInterfaceClass_ == USB_CLASS_HID_VALUE) {
        currentClaimResult_ = usb_host_interface_claim(clientHandle_, deviceHandle_, currentInterfaceNumber_, intf->bAlternateSetting);
        if (currentClaimResult_ == ESP_OK && interfaceCount_ < sizeof(interfaces_)) {
          interfaces_[interfaceCount_++] = currentInterfaceNumber_;
          ESP_LOGI(TAG, "Interface %u claimed", currentInterfaceNumber_);
          if (currentInterfaceSubClass_ == HID_SUBCLASS_BOOT_VALUE &&
              currentInterfaceProtocol_ == HID_PROTOCOL_KEYBOARD_VALUE &&
              !hasKeyboardInterface_) {
            hasKeyboardInterface_ = true;
            keyboardInterfaceNumber_ = currentInterfaceNumber_;
            ESP_LOGI(TAG, "Keyboard interface ready: iface=%u", keyboardInterfaceNumber_);
          }
        } else {
          ESP_LOGW(TAG, "usb_host_interface_claim(%u) failed: %s", currentInterfaceNumber_, esp_err_to_name(currentClaimResult_));
          setLastError(currentClaimResult_);
        }
      }
      break;
    }

    case USB_ENDPOINT_DESC: {
      const usb_ep_desc_t *ep = reinterpret_cast<const usb_ep_desc_t *>(data);
      const bool isIn = (ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0;
      const bool isInterrupt = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_INT;
      if (currentClaimResult_ != ESP_OK || currentInterfaceClass_ != USB_CLASS_HID_VALUE || !isIn || !isInterrupt) {
        ESP_LOGD(TAG, "Skipping endpoint 0x%02x iface=%u class=0x%02x attrs=0x%02x",
                 ep->bEndpointAddress,
                 currentInterfaceNumber_,
                 currentInterfaceClass_,
                 ep->bmAttributes);
        return;
      }

      EndpointState *endpoint = allocateEndpoint();
      if (!endpoint) {
        ESP_LOGW(TAG, "No endpoint slots available");
        setLastError(ESP_ERR_NO_MEM);
        return;
      }

      esp_err_t err = usb_host_transfer_alloc(ep->wMaxPacketSize, 0, &endpoint->transfer);
      if (err != ESP_OK) {
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
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "usb_host_transfer_submit() failed: %s", esp_err_to_name(err));
        setLastError(err);
      } else {
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

void EspUsbHost::transferCallback(usb_transfer_t *transfer) {
  static_cast<EspUsbHost *>(transfer->context)->handleTransfer(transfer);
}

void EspUsbHost::controlTransferCallback(usb_transfer_t *transfer) {
  EspUsbHost *host = static_cast<EspUsbHost *>(transfer->context);
  if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
    ESP_LOGD(TAG, "control transfer status=%d", transfer->status);
    if (host) {
      host->setLastError(ESP_FAIL);
    }
  }
  usb_host_transfer_free(transfer);
}

void EspUsbHost::handleTransfer(usb_transfer_t *transfer) {
  EndpointState *endpoint = findEndpoint(transfer->bEndpointAddress);
  if (!endpoint) {
    return;
  }

  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes > 0) {
    ESP_LOGV(TAG, "HID input iface=%u ep=0x%02x len=%u",
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
    if (hidInputCallback_) {
      hidInputCallback_(input);
    }

    if (endpoint->interfaceClass == USB_CLASS_HID_VALUE) {
      if (transfer->actual_num_bytes >= 5 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_MOUSE) {
        handleMouse(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
      } else if (transfer->actual_num_bytes >= 9 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_KEYBOARD) {
        handleKeyboard(*endpoint, transfer->data_buffer + 1, transfer->actual_num_bytes - 1);
      } else if (transfer->actual_num_bytes >= 3 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_CONSUMER_CONTROL) {
        handleConsumerControl(*endpoint, transfer->data_buffer + 1, transfer->actual_num_bytes - 1);
      } else if (transfer->actual_num_bytes >= 12 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_GAMEPAD) {
        handleGamepad(*endpoint, transfer->data_buffer + 1, transfer->actual_num_bytes - 1);
      } else if (transfer->actual_num_bytes >= 2 && transfer->data_buffer[0] == ESP_USB_HOST_HID_REPORT_ID_VENDOR) {
        handleVendorInput(*endpoint, transfer->data_buffer + 1, transfer->actual_num_bytes - 1);
      } else if (endpoint->interfaceSubClass == HID_SUBCLASS_BOOT_VALUE &&
                 endpoint->interfaceProtocol == HID_PROTOCOL_KEYBOARD_VALUE) {
        handleKeyboard(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
      } else if (endpoint->interfaceSubClass == HID_SUBCLASS_BOOT_VALUE &&
                 endpoint->interfaceProtocol == HID_PROTOCOL_MOUSE_VALUE) {
        handleMouse(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
      }
    }
  } else if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
    ESP_LOGD(TAG, "transfer status=%d ep=0x%02x", transfer->status, transfer->bEndpointAddress);
  }

  if (transfer->status == USB_TRANSFER_STATUS_NO_DEVICE ||
      transfer->status == USB_TRANSFER_STATUS_CANCELED) {
    return;
  }

  if (running_ && deviceHandle_) {
    esp_err_t err = usb_host_transfer_submit(transfer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE && err != ESP_ERR_NOT_FINISHED) {
      ESP_LOGD(TAG, "usb_host_transfer_submit() failed: %s", esp_err_to_name(err));
      setLastError(err);
    }
  }
}

void EspUsbHost::handleKeyboard(EndpointState &endpoint, const uint8_t *data, size_t length) {
  if (length < ESP_USB_HOST_BOOT_KEYBOARD_REPORT_SIZE) {
    return;
  }

  if (!espUsbHostIsBootKeyboardReportValid(data, length)) {
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

  if (!endpoint.keyboardReportReady) {
    endpoint.keyboardReportReady = true;
  }

  for (size_t i = 0; i < eventCount; i++) {
    ESP_LOGD(TAG, "Keyboard %s iface=%u keycode=0x%02x ascii=0x%02x modifiers=0x%02x",
             events[i].pressed ? "press" : "release",
             events[i].interfaceNumber,
             events[i].keycode,
             events[i].ascii,
             events[i].modifiers);
    if (keyboardCallback_) {
      keyboardCallback_(events[i]);
    }
  }

  memcpy(endpoint.lastKeyboardReport, data, ESP_USB_HOST_BOOT_KEYBOARD_REPORT_SIZE);
}

void EspUsbHost::handleMouse(EndpointState &endpoint, const uint8_t *data, size_t length) {
  if (!mouseCallback_) {
    return;
  }

  if (endpoint.interfaceProtocol != HID_PROTOCOL_MOUSE_VALUE &&
      length >= 5 &&
      data[0] == ESP_USB_HOST_HID_REPORT_ID_MOUSE) {
    data += 1;
    length -= 1;
  }

  EspUsbHostMouseEvent event;
  if (!espUsbHostParseBootMouseReport(endpoint.interfaceNumber,
                                      data,
                                      length,
                                      endpoint.lastMouseButtons,
                                      event)) {
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

void EspUsbHost::handleConsumerControl(EndpointState &endpoint, const uint8_t *data, size_t length) {
  if (!consumerControlCallback_) {
    return;
  }

  EspUsbHostConsumerControlEvent event;
  if (!espUsbHostParseConsumerControlReport(endpoint.interfaceNumber,
                                            data,
                                            length,
                                            endpoint.lastConsumerUsage,
                                            event)) {
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

void EspUsbHost::handleGamepad(EndpointState &endpoint, const uint8_t *data, size_t length) {
  if (!gamepadCallback_) {
    return;
  }

  EspUsbHostGamepadEvent event;
  if (!espUsbHostParseGamepadReport(endpoint.interfaceNumber,
                                    data,
                                    length,
                                    endpoint.lastGamepadButtons,
                                    event)) {
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

void EspUsbHost::handleVendorInput(EndpointState &endpoint, const uint8_t *data, size_t length) {
  if (!vendorInputCallback_) {
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

EspUsbHost::EndpointState *EspUsbHost::findEndpoint(uint8_t endpointAddress) {
  for (EndpointState &endpoint : endpoints_) {
    if (endpoint.inUse && endpoint.address == endpointAddress) {
      return &endpoint;
    }
  }
  return nullptr;
}

EspUsbHost::EndpointState *EspUsbHost::allocateEndpoint() {
  for (EndpointState &endpoint : endpoints_) {
    if (!endpoint.inUse) {
      endpoint = EndpointState();
      endpoint.inUse = true;
      return &endpoint;
    }
  }
  return nullptr;
}

void EspUsbHost::releaseEndpoints(bool clearEndpoints) {
  for (EndpointState &endpoint : endpoints_) {
    if (!endpoint.inUse) {
      continue;
    }
    if (endpoint.transfer) {
      if (clearEndpoints && deviceHandle_) {
        esp_err_t err = usb_host_endpoint_clear(deviceHandle_, endpoint.address);
        if (err != ESP_OK) {
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

void EspUsbHost::releaseInterfaces() {
  for (uint8_t i = 0; i < interfaceCount_; i++) {
    usb_host_interface_release(clientHandle_, deviceHandle_, interfaces_[i]);
    interfaces_[i] = 0;
  }
  interfaceCount_ = 0;
}

void EspUsbHost::setLastError(esp_err_t err) {
  if (err != ESP_OK) {
    lastError_ = err;
  }
}

String EspUsbHost::usbString(const usb_str_desc_t *strDesc) {
  String result;
  if (!strDesc) {
    return result;
  }
  for (int i = 0; i < strDesc->bLength / 2; i++) {
    if (strDesc->wData[i] <= 0xff) {
      result += static_cast<char>(strDesc->wData[i]);
    }
  }
  return result;
}
