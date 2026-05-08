#include "EspUsbHost.h"

static const char *TAG = "EspUsbHost";

static constexpr uint8_t HID_BOOT_KEYBOARD_REPORT_SIZE = 8;
static constexpr uint8_t USB_CLASS_HID_VALUE = 0x03;
static constexpr uint8_t USB_CLASS_HUB_VALUE = 0x09;
static constexpr uint8_t HID_SUBCLASS_BOOT_VALUE = 0x01;
static constexpr uint8_t HID_PROTOCOL_KEYBOARD_VALUE = 0x01;
static constexpr uint8_t HID_PROTOCOL_MOUSE_VALUE = 0x02;

static constexpr uint8_t MOD_LEFT_SHIFT = 0x02;
static constexpr uint8_t MOD_RIGHT_SHIFT = 0x20;

static const uint8_t KEYCODE_TO_ASCII_US[128][2] = { HID_KEYCODE_TO_ASCII };

static uint8_t keypadKeycodeToAscii(uint8_t keycode) {
  switch (keycode) {
    case 0x54: return '/';
    case 0x55: return '*';
    case 0x56: return '-';
    case 0x57: return '+';
    case 0x58: return '\r';
    case 0x59: return '1';
    case 0x5a: return '2';
    case 0x5b: return '3';
    case 0x5c: return '4';
    case 0x5d: return '5';
    case 0x5e: return '6';
    case 0x5f: return '7';
    case 0x60: return '8';
    case 0x61: return '9';
    case 0x62: return '0';
    case 0x63: return '.';
    case 0x67: return '=';
    default: return 0;
  }
}

static bool isBootKeyboardReportValid(const uint8_t *data) {
  if (data[1] != 0) {
    return false;
  }
  for (int i = 2; i < HID_BOOT_KEYBOARD_REPORT_SIZE; i++) {
    const uint8_t keycode = data[i];
    if (keycode >= 0xe0) {
      return false;
    }
  }
  return true;
}

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

static const uint8_t KEYCODE_TO_ASCII_JP[128][2] = {
  {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {'a', 'A'}, {'b', 'B'}, {'c', 'C'}, {'d', 'D'}, {'e', 'E'}, {'f', 'F'},
  {'g', 'G'}, {'h', 'H'}, {'i', 'I'}, {'j', 'J'}, {'k', 'K'}, {'l', 'L'},
  {'m', 'M'}, {'n', 'N'}, {'o', 'O'}, {'p', 'P'}, {'q', 'Q'}, {'r', 'R'},
  {'s', 'S'}, {'t', 'T'}, {'u', 'U'}, {'v', 'V'}, {'w', 'W'}, {'x', 'X'},
  {'y', 'Y'}, {'z', 'Z'}, {'1', '!'}, {'2', '"'}, {'3', '#'}, {'4', '$'},
  {'5', '%'}, {'6', '&'}, {'7', '\''}, {'8', '('}, {'9', ')'}, {'0', 0},
  {'\r', '\r'}, {'\x1b', '\x1b'}, {'\b', '\b'}, {'\t', '\t'}, {' ', ' '},
  {'-', '='}, {'^', '~'}, {'@', '`'}, {'[', '{'}, {0, 0}, {']', '}'},
  {';', '+'}, {':', '*'}, {0, 0}, {',', '<'}, {'.', '>'}, {'/', '?'},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
  {'/', '/'}, {'*', '*'}, {'-', '-'}, {'+', '+'}, {'\r', '\r'},
  {'1', 0}, {'2', 0}, {'3', 0}, {'4', 0}, {'5', '5'}, {'6', 0},
  {'7', 0}, {'8', 0}, {'9', 0}, {'0', 0}, {'.', 0}, {0, 0}, {0, 0},
  {0, 0}, {'=', '='},
};

EspUsbHost::EspUsbHost() = default;

EspUsbHost::~EspUsbHost() {
  end();
}

bool EspUsbHost::begin() {
  return begin(EspUsbHostConfig());
}

bool EspUsbHost::begin(const EspUsbHostConfig &config) {
  if (running_) {
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

void EspUsbHost::setKeyboardLayout(EspUsbHostKeyboardLayout layout) {
  keyboardLayout_ = layout;
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
  ESP_LOGI(TAG, "USB Host started");

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
  releaseEndpoints();
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
  releaseEndpoints();
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
}

void EspUsbHost::parseConfigDescriptor(const usb_config_desc_t *configDesc) {
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

void EspUsbHost::handleTransfer(usb_transfer_t *transfer) {
  EndpointState *endpoint = findEndpoint(transfer->bEndpointAddress);
  if (!endpoint) {
    return;
  }

  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED && transfer->actual_num_bytes > 0) {
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

    if (endpoint->interfaceClass == USB_CLASS_HID_VALUE && endpoint->interfaceSubClass == HID_SUBCLASS_BOOT_VALUE) {
      if (endpoint->interfaceProtocol == HID_PROTOCOL_KEYBOARD_VALUE) {
        handleKeyboard(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
      } else if (endpoint->interfaceProtocol == HID_PROTOCOL_MOUSE_VALUE) {
        handleMouse(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
      }
    }
  } else if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
    ESP_LOGD(TAG, "transfer status=%d ep=0x%02x", transfer->status, transfer->bEndpointAddress);
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
  if (length < HID_BOOT_KEYBOARD_REPORT_SIZE) {
    return;
  }

  if (!isBootKeyboardReportValid(data)) {
    ESP_LOGD(TAG, "Ignoring invalid boot keyboard report: %02x %02x %02x %02x %02x %02x %02x %02x",
             data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
    return;
  }

  if (!endpoint.keyboardReportReady) {
    endpoint.keyboardReportReady = true;
  }

  const uint8_t modifiers = data[0];
  const uint8_t *keys = data + 2;
  const uint8_t *lastKeys = endpoint.lastKeyboardReport + 2;

  for (int i = 0; i < 6; i++) {
    const uint8_t key = keys[i];
    if (key == 0) {
      continue;
    }
    bool existed = false;
    for (int j = 0; j < 6; j++) {
      if (lastKeys[j] == key) {
        existed = true;
        break;
      }
    }
    if (!existed && keyboardCallback_) {
      EspUsbHostKeyboardEvent event;
      event.interfaceNumber = endpoint.interfaceNumber;
      event.pressed = true;
      event.keycode = key;
      event.ascii = keycodeToAscii(key, modifiers);
      event.modifiers = modifiers;
      keyboardCallback_(event);
    }
  }

  for (int i = 0; i < 6; i++) {
    const uint8_t key = lastKeys[i];
    if (key == 0) {
      continue;
    }
    bool stillPressed = false;
    for (int j = 0; j < 6; j++) {
      if (keys[j] == key) {
        stillPressed = true;
        break;
      }
    }
    if (!stillPressed && keyboardCallback_) {
      EspUsbHostKeyboardEvent event;
      event.interfaceNumber = endpoint.interfaceNumber;
      event.released = true;
      event.keycode = key;
      event.ascii = keycodeToAscii(key, endpoint.lastKeyboardReport[0]);
      event.modifiers = modifiers;
      keyboardCallback_(event);
    }
  }

  memcpy(endpoint.lastKeyboardReport, data, HID_BOOT_KEYBOARD_REPORT_SIZE);
}

void EspUsbHost::handleMouse(EndpointState &endpoint, const uint8_t *data, size_t length) {
  if (length < 3 || !mouseCallback_) {
    return;
  }

  EspUsbHostMouseEvent event;
  event.interfaceNumber = endpoint.interfaceNumber;
  event.previousButtons = endpoint.lastMouseButtons;
  event.buttons = data[0];
  event.x = static_cast<int8_t>(data[1]);
  event.y = static_cast<int8_t>(data[2]);
  event.wheel = length > 3 ? static_cast<int8_t>(data[3]) : 0;
  event.moved = event.x != 0 || event.y != 0 || event.wheel != 0;
  event.buttonsChanged = event.buttons != event.previousButtons;

  if (event.moved || event.buttonsChanged) {
    mouseCallback_(event);
  }
  endpoint.lastMouseButtons = event.buttons;
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

void EspUsbHost::releaseEndpoints() {
  for (EndpointState &endpoint : endpoints_) {
    if (!endpoint.inUse) {
      continue;
    }
    if (endpoint.transfer) {
      usb_host_endpoint_clear(deviceHandle_, endpoint.address);
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

uint8_t EspUsbHost::keycodeToAscii(uint8_t keycode, uint8_t modifiers) const {
  if (keycode >= 128) {
    return 0;
  }
  const uint8_t keypadAscii = keypadKeycodeToAscii(keycode);
  if (keypadAscii) {
    return keypadAscii;
  }
  const uint8_t shift = (modifiers & (MOD_LEFT_SHIFT | MOD_RIGHT_SHIFT)) ? 1 : 0;
  if (keyboardLayout_ == ESP_USB_HOST_KEYBOARD_LAYOUT_JP) {
    return KEYCODE_TO_ASCII_JP[keycode][shift];
  }
  return KEYCODE_TO_ASCII_US[keycode][shift];
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
