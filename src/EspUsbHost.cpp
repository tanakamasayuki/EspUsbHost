#include "EspUsbHost.h"
#include "EspUsbHostHid.h"

#if CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_ERROR
static const char *TAG = "EspUsbHost";
#endif

static constexpr uint8_t USB_CLASS_HID_VALUE = 0x03;
static constexpr uint8_t USB_CLASS_HUB_VALUE = 0x09;
static constexpr uint8_t USB_CLASS_AUDIO_VALUE = 0x01;
static constexpr uint8_t USB_CLASS_CDC_CONTROL_VALUE = 0x02;
static constexpr uint8_t USB_CLASS_CDC_DATA_VALUE = 0x0a;
static constexpr uint8_t USB_CLASS_VENDOR_VALUE = 0xff;
static constexpr uint8_t USB_CS_INTERFACE_DESC = 0x24;
static constexpr uint8_t USB_AUDIO_SUBCLASS_AUDIO_STREAMING = 0x02;
static constexpr uint8_t USB_AUDIO_SUBCLASS_MIDI_STREAMING = 0x03;
static constexpr uint8_t USB_AUDIO_CS_AS_FORMAT_TYPE = 0x02;
static constexpr uint8_t USB_AUDIO_FORMAT_TYPE_I = 0x01;
static constexpr uint8_t HID_SUBCLASS_BOOT_VALUE = 0x01;
static constexpr uint8_t HID_PROTOCOL_KEYBOARD_VALUE = 0x01;
static constexpr uint8_t HID_PROTOCOL_MOUSE_VALUE = 0x02;
static constexpr uint8_t HID_CLASS_REQUEST_SET_REPORT = 0x09;
static constexpr uint8_t HID_CLASS_REQUEST_SET_PROTOCOL = 0x0B;
static constexpr uint8_t HID_PROTOCOL_REPORT_MODE = 0x01;
static constexpr uint8_t HID_SET_REPORT_REQUEST_TYPE = 0x21;
static constexpr uint8_t CDC_CLASS_REQUEST_SET_LINE_CODING = 0x20;
static constexpr uint8_t CDC_CLASS_REQUEST_SET_CONTROL_LINE_STATE = 0x22;
static constexpr uint8_t CDC_SET_REQUEST_TYPE = 0x21;
static constexpr uint8_t VENDOR_OUT_REQUEST_TYPE = 0x40;
static constexpr uint8_t VENDOR_INTERFACE_OUT_REQUEST_TYPE = 0x41;
static constexpr uint8_t MIDI_CIN_SYSEX_START = 0x04;
static constexpr uint8_t MIDI_CIN_SYSEX_END_1BYTE = 0x05;
static constexpr uint8_t MIDI_CIN_SYSEX_END_2BYTE = 0x06;
static constexpr uint8_t MIDI_CIN_SYSEX_END_3BYTE = 0x07;
static constexpr uint8_t MIDI_CIN_NOTE_OFF = 0x08;
static constexpr uint8_t MIDI_CIN_NOTE_ON = 0x09;
static constexpr uint8_t MIDI_CIN_POLY_KEYPRESS = 0x0a;
static constexpr uint8_t MIDI_CIN_CONTROL_CHANGE = 0x0b;
static constexpr uint8_t MIDI_CIN_PROGRAM_CHANGE = 0x0c;
static constexpr uint8_t MIDI_CIN_CHANNEL_PRESSURE = 0x0d;
static constexpr uint8_t MIDI_CIN_PITCH_BEND_CHANGE = 0x0e;

static unsigned hostPeripheralMap(EspUsbHostPort port)
{
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  switch (port)
  {
  case ESP_USB_HOST_PORT_HIGH_SPEED:
    return 1U << 0;
  case ESP_USB_HOST_PORT_FULL_SPEED:
    return 1U << 1;
  case ESP_USB_HOST_PORT_DEFAULT:
  default:
    return 0;
  }
#else
  (void)port;
  return 0;
#endif
}

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

#if CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
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
#endif

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
  nextHubIndex_ = 1;

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

void EspUsbHost::onMidiMessage(MidiMessageCallback callback)
{
  midiMessageCallback_ = callback;
}

void EspUsbHost::onAudioData(AudioDataCallback callback)
{
  audioDataCallback_ = callback;
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
                               size_t length,
                               uint8_t address)
{
  DeviceState *device = findDevice(address);
  if (!running_ || !device || !device->handle || !clientHandle_)
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

  transfer->device_handle = device->handle;
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

bool EspUsbHost::setKeyboardLeds(bool numLock, bool capsLock, bool scrollLock, uint8_t address)
{
  DeviceState *device = findKeyboardDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "setKeyboardLeds() called before a keyboard interface is ready");
    return false;
  }

  uint8_t leds = espUsbHostBuildKeyboardLedReport(numLock, capsLock, scrollLock);

  if (!device->keyboardReportProtocolSet)
  {
    device->keyboardLedPending = true;
    device->keyboardLedPendingMask = leds;
    return sendSetProtocol(device->keyboardInterfaceNumber, device->info.address);
  }

  return sendKeyboardLedReport(*device, leds);
}

bool EspUsbHost::sendSetProtocol(uint8_t interfaceNumber, uint8_t address)
{
  DeviceState *device = findDevice(address);
  if (!running_ || !device || !device->handle || !clientHandle_)
  {
    return false;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE, 0, &transfer);
  if (err != ESP_OK)
  {
    return false;
  }

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = HID_SET_REPORT_REQUEST_TYPE;
  setup->bRequest = HID_CLASS_REQUEST_SET_PROTOCOL;
  setup->wValue = HID_PROTOCOL_REPORT_MODE;
  setup->wIndex = interfaceNumber;
  setup->wLength = 0;

  transfer->device_handle = device->handle;
  transfer->bEndpointAddress = 0;
  transfer->callback = controlTransferCallback;
  transfer->context = this;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    usb_host_transfer_free(transfer);
    return false;
  }
  return true;
}

bool EspUsbHost::sendKeyboardLedReport(DeviceState &device, uint8_t leds)
{
  return sendHIDReport(device.keyboardInterfaceNumber,
                       ESP_USB_HOST_HID_REPORT_TYPE_OUTPUT,
                       ESP_USB_HOST_HID_REPORT_ID_KEYBOARD,
                       &leds,
                       sizeof(leds),
                       device.info.address);
}

bool EspUsbHost::sendVendorOutput(const uint8_t *data, size_t length, uint8_t address)
{
  DeviceState *device = findVendorDevice(address);
  if (!device)
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

  return sendHIDReport(device->vendorInterfaceNumber,
                       ESP_USB_HOST_HID_REPORT_TYPE_OUTPUT,
                       0,
                       report,
                       length + 1,
                       device->info.address);
}

bool EspUsbHost::sendVendorFeature(const uint8_t *data, size_t length, uint8_t address)
{
  DeviceState *device = findVendorDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "sendVendorFeature() called before a vendor HID interface is ready");
    return false;
  }
  return sendHIDReport(device->vendorInterfaceNumber,
                       ESP_USB_HOST_HID_REPORT_TYPE_FEATURE,
                       ESP_USB_HOST_HID_REPORT_ID_VENDOR,
                       data,
                       length,
                       device->info.address);
}

bool EspUsbHost::sendSerial(const uint8_t *data, size_t length, uint8_t address)
{
  DeviceState *device = findSerialDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "sendSerial() called before a CDC OUT endpoint is ready");
    return false;
  }
  if (length > 0 && !data)
  {
    ESP_LOGW(TAG, "sendSerial() called with null data");
    return false;
  }

  const size_t packetSize = length > device->serialOutPacketSize ? length : device->serialOutPacketSize;
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
  transfer->device_handle = device->handle;
  transfer->bEndpointAddress = device->serialOutEndpointAddress;
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

bool EspUsbHost::sendSerial(const char *text, uint8_t address)
{
  if (!text)
  {
    return false;
  }
  return sendSerial(reinterpret_cast<const uint8_t *>(text), strlen(text), address);
}

bool EspUsbHost::serialReady(uint8_t address) const
{
  return findSerialDevice(address) != nullptr;
}

bool EspUsbHost::setSerialBaudRate(uint32_t baud, uint8_t address)
{
  if (baud == 0)
  {
    ESP_LOGW(TAG, "setSerialBaudRate() called with baud=0");
    return false;
  }

  DeviceState *device = findDevice(address);
  if (address == ESP_USB_HOST_ANY_ADDRESS)
  {
    defaultSerialBaudRate_ = baud;
  }
  if (!device)
  {
    return true;
  }

  device->serialBaudRate = baud;
  if (device->hasCdcControlInterface)
  {
    device->cdcConfigured = false;
    configureCdcAcm(*device);
  }
  else if (device->vendorSerialSupported)
  {
    configureVendorSerial(*device);
  }
  return true;
}

bool EspUsbHost::midiReady(uint8_t address) const
{
  return findMidiDevice(address) != nullptr;
}

bool EspUsbHost::audioReady(uint8_t address) const
{
  return findAudioDevice(address) != nullptr;
}

bool EspUsbHost::audioOutputReady(uint8_t address) const
{
  return findAudioOutputDevice(address) != nullptr;
}

bool EspUsbHost::setAudioSampleRate(uint32_t sampleRate, uint8_t address)
{
  if (sampleRate == 0 || sampleRate > 0x00ffffff)
  {
    ESP_LOGW(TAG, "setAudioSampleRate() called with invalid sampleRate=%lu",
             static_cast<unsigned long>(sampleRate));
    return false;
  }

  if (address == ESP_USB_HOST_ANY_ADDRESS)
  {
    defaultAudioSampleRate_ = sampleRate;
  }

  DeviceState *device = findDevice(address);
  if (!device)
  {
    return true;
  }

  device->audioSampleRate = sampleRate;
  bool submitted = true;
  if (device->hasAudioOutEndpoint)
  {
    submitted = submitAudioSamplingFrequency(*device, device->audioOutEndpointAddress, device->audioSampleRate) && submitted;
  }
  for (EndpointState &endpoint : endpoints_)
  {
    if (!endpoint.inUse ||
        endpoint.deviceHandle != device->handle ||
        endpoint.interfaceClass != USB_CLASS_AUDIO_VALUE ||
        endpoint.interfaceSubClass != USB_AUDIO_SUBCLASS_AUDIO_STREAMING ||
        (endpoint.address & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) == 0)
    {
      continue;
    }
    submitted = submitAudioSamplingFrequency(*device, endpoint.address, device->audioSampleRate) && submitted;
  }
  return submitted;
}

bool EspUsbHost::audioSend(const uint8_t *data, size_t length, uint8_t address)
{
  DeviceState *device = findAudioOutputDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "audioSend() called before a USB Audio OUT endpoint is ready");
    return false;
  }
  if (length > 0 && !data)
  {
    ESP_LOGW(TAG, "audioSend() called with null data");
    return false;
  }
  if (length == 0)
  {
    return true;
  }

  const size_t packetSize = device->audioOutPacketSize;
  if (packetSize == 0)
  {
    ESP_LOGW(TAG, "audioSend() called with invalid audio OUT packet size");
    return false;
  }

  static constexpr int AUDIO_ISOC_MAX_PACKETS = 8;
  const size_t maxTransferSize = packetSize * AUDIO_ISOC_MAX_PACKETS;
  size_t offset = 0;
  while (offset < length)
  {
    const size_t chunkLength = (length - offset) > maxTransferSize ? maxTransferSize : (length - offset);
    if (!submitAudioOutputTransfer(*device, data + offset, chunkLength))
    {
      return false;
    }
    offset += chunkLength;
  }
  return true;
}

bool EspUsbHost::submitAudioOutputTransfer(DeviceState &device, const uint8_t *data, size_t length)
{
  const size_t packetSize = device.audioOutPacketSize;
  const int packetCount = static_cast<int>((length + packetSize - 1) / packetSize);
  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(packetCount * packetSize, packetCount, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(audio OUT) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  memcpy(transfer->data_buffer, data, length);
  transfer->device_handle = device.handle;
  transfer->bEndpointAddress = device.audioOutEndpointAddress;
  transfer->callback = outputTransferCallback;
  transfer->context = this;
  transfer->num_bytes = length;

  size_t remaining = length;
  for (int i = 0; i < packetCount; i++)
  {
    const size_t currentPacketSize = remaining > packetSize ? packetSize : remaining;
    transfer->isoc_packet_desc[i].num_bytes = currentPacketSize;
    transfer->isoc_packet_desc[i].actual_num_bytes = 0;
    transfer->isoc_packet_desc[i].status = USB_TRANSFER_STATUS_COMPLETED;
    remaining -= currentPacketSize;
  }

  err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(audio OUT) failed: %s", esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }
  return true;
}

bool EspUsbHost::midiSend(const uint8_t *data, size_t length, uint8_t address)
{
  DeviceState *device = findMidiDevice(address);
  if (!device)
  {
    ESP_LOGW(TAG, "midiSend() called before a MIDI OUT endpoint is ready");
    return false;
  }
  if (length > 0 && !data)
  {
    ESP_LOGW(TAG, "midiSend() called with null data");
    return false;
  }

  const size_t packetSize = length > device->midiOutPacketSize ? length : device->midiOutPacketSize;
  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(packetSize, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(MIDI OUT) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  if (length > 0)
  {
    memcpy(transfer->data_buffer, data, length);
  }
  transfer->device_handle = device->handle;
  transfer->bEndpointAddress = device->midiOutEndpointAddress;
  transfer->callback = serialOutTransferCallback;
  transfer->context = this;
  transfer->num_bytes = length;

  err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(MIDI OUT) failed: %s", esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }
  return true;
}

bool EspUsbHost::midiSendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t address)
{
  const uint8_t packet[4] = {
      MIDI_CIN_NOTE_ON,
      static_cast<uint8_t>(0x90 | (channel & 0x0f)),
      static_cast<uint8_t>(note & 0x7f),
      static_cast<uint8_t>(velocity & 0x7f)};
  return midiSend(packet, sizeof(packet), address);
}

bool EspUsbHost::midiSendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t address)
{
  const uint8_t packet[4] = {
      MIDI_CIN_NOTE_OFF,
      static_cast<uint8_t>(0x80 | (channel & 0x0f)),
      static_cast<uint8_t>(note & 0x7f),
      static_cast<uint8_t>(velocity & 0x7f)};
  return midiSend(packet, sizeof(packet), address);
}

bool EspUsbHost::midiSendControlChange(uint8_t channel, uint8_t control, uint8_t value, uint8_t address)
{
  const uint8_t packet[4] = {
      MIDI_CIN_CONTROL_CHANGE,
      static_cast<uint8_t>(0xb0 | (channel & 0x0f)),
      static_cast<uint8_t>(control & 0x7f),
      static_cast<uint8_t>(value & 0x7f)};
  return midiSend(packet, sizeof(packet), address);
}

bool EspUsbHost::midiSendProgramChange(uint8_t channel, uint8_t program, uint8_t address)
{
  const uint8_t packet[4] = {
      MIDI_CIN_PROGRAM_CHANGE,
      static_cast<uint8_t>(0xc0 | (channel & 0x0f)),
      static_cast<uint8_t>(program & 0x7f),
      0};
  return midiSend(packet, sizeof(packet), address);
}

bool EspUsbHost::midiSendPolyPressure(uint8_t channel, uint8_t note, uint8_t pressure, uint8_t address)
{
  const uint8_t packet[4] = {
      MIDI_CIN_POLY_KEYPRESS,
      static_cast<uint8_t>(0xa0 | (channel & 0x0f)),
      static_cast<uint8_t>(note & 0x7f),
      static_cast<uint8_t>(pressure & 0x7f)};
  return midiSend(packet, sizeof(packet), address);
}

bool EspUsbHost::midiSendChannelPressure(uint8_t channel, uint8_t pressure, uint8_t address)
{
  const uint8_t packet[4] = {
      MIDI_CIN_CHANNEL_PRESSURE,
      static_cast<uint8_t>(0xd0 | (channel & 0x0f)),
      static_cast<uint8_t>(pressure & 0x7f),
      0};
  return midiSend(packet, sizeof(packet), address);
}

bool EspUsbHost::midiSendPitchBend(uint8_t channel, uint16_t value, uint8_t address)
{
  if (value > 16383)
  {
    value = 16383;
  }
  const uint8_t packet[4] = {
      MIDI_CIN_PITCH_BEND_CHANGE,
      static_cast<uint8_t>(0xe0 | (channel & 0x0f)),
      static_cast<uint8_t>(value & 0x7f),
      static_cast<uint8_t>((value >> 7) & 0x7f)};
  return midiSend(packet, sizeof(packet), address);
}

bool EspUsbHost::midiSendPitchBendSigned(uint8_t channel, int16_t value, uint8_t address)
{
  if (value < -8192)
  {
    value = -8192;
  }
  else if (value > 8191)
  {
    value = 8191;
  }
  return midiSendPitchBend(channel, static_cast<uint16_t>(value + 8192), address);
}

bool EspUsbHost::midiSendSysEx(const uint8_t *data, size_t length, uint8_t address)
{
  if (length == 0)
  {
    return false;
  }
  if (!data)
  {
    ESP_LOGW(TAG, "midiSendSysEx() called with null data");
    return false;
  }
  if (!findMidiDevice(address))
  {
    ESP_LOGW(TAG, "midiSendSysEx() called before a MIDI OUT endpoint is ready");
    return false;
  }

  const size_t packetCount = (length + 2) / 3;
  if (packetCount == 0 || packetCount > 64)
  {
    ESP_LOGW(TAG, "midiSendSysEx() unsupported length=%u", static_cast<unsigned>(length));
    return false;
  }

  uint8_t packets[64 * 4] = {};
  size_t offset = 0;
  size_t out = 0;
  while (offset < length)
  {
    const size_t remaining = length - offset;
    const size_t chunk = remaining >= 3 ? 3 : remaining;
    uint8_t cin = MIDI_CIN_SYSEX_START;
    if (remaining <= 3)
    {
      cin = chunk == 1 ? MIDI_CIN_SYSEX_END_1BYTE : chunk == 2 ? MIDI_CIN_SYSEX_END_2BYTE
                                                               : MIDI_CIN_SYSEX_END_3BYTE;
    }
    packets[out++] = cin;
    packets[out++] = data[offset];
    packets[out++] = chunk > 1 ? data[offset + 1] : 0;
    packets[out++] = chunk > 2 ? data[offset + 2] : 0;
    offset += chunk;
  }

  return midiSend(packets, out, address);
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
  hostConfig.peripheral_map = hostPeripheralMap(config_.port);

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
  releaseAllEndpoints(true);
  for (DeviceState &device : devices_)
  {
    if (!device.inUse)
    {
      continue;
    }
    releaseInterfaces(device);
    if (device.handle)
    {
      usb_host_device_close(clientHandle_, device.handle);
    }
    device = DeviceState();
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
  DeviceState *device = allocateDevice();
  if (!device)
  {
    ESP_LOGW(TAG, "Ignoring device at address=%u because no device slots are available", address);
    setLastError(ESP_ERR_NO_MEM);
    return;
  }
  device->inUse = true;
  device->info.address = address;
  device->serialBaudRate = defaultSerialBaudRate_;
  device->audioSampleRate = defaultAudioSampleRate_;

  esp_err_t err = usb_host_device_open(clientHandle_, address, &device->handle);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "usb_host_device_open() failed: %s", esp_err_to_name(err));
    setLastError(err);
    *device = DeviceState();
    return;
  }

  usb_device_info_t devInfo = {};
  err = usb_host_device_info(device->handle, &devInfo);
  if (err == ESP_OK)
  {
    device->manufacturer = usbString(devInfo.str_desc_manufacturer);
    device->product = usbString(devInfo.str_desc_product);
    device->serial = usbString(devInfo.str_desc_serial_num);
    device->info.address = devInfo.dev_addr;
    device->info.speed = devInfo.speed;
    device->info.maxPacketSize0 = devInfo.bMaxPacketSize0;
    device->info.configurationValue = devInfo.bConfigurationValue;
    const uint8_t upstreamPort = devInfo.parent.port_num;
    if (devInfo.parent.dev_hdl)
    {
      DeviceState *parent = findDeviceByHandle(devInfo.parent.dev_hdl);
      device->info.parentAddress = parent ? parent->info.address : 0;
      device->info.portId = parent && parent->hubIndex && upstreamPort > 0 && upstreamPort <= 0x0f
                                ? static_cast<uint8_t>((parent->hubIndex << 4) | upstreamPort)
                                : upstreamPort;
    }
    else
    {
      device->info.portId = upstreamPort ? upstreamPort : 0x01;
    }
  }

  const usb_device_desc_t *devDesc = nullptr;
  err = usb_host_get_device_descriptor(device->handle, &devDesc);
  if (err == ESP_OK && devDesc)
  {
    device->info.address = address;
    device->info.vid = devDesc->idVendor;
    device->info.pid = devDesc->idProduct;
    device->info.manufacturer = device->manufacturer.c_str();
    device->info.product = device->product.c_str();
    device->info.serial = device->serial.c_str();
    device->info.usbVersion = devDesc->bcdUSB;
    device->info.deviceVersion = devDesc->bcdDevice;
    device->info.deviceClass = devDesc->bDeviceClass;
    device->info.deviceSubClass = devDesc->bDeviceSubClass;
    device->info.deviceProtocol = devDesc->bDeviceProtocol;
    device->info.maxPacketSize0 = devDesc->bMaxPacketSize0;
    ESP_LOGI(TAG, "VID=%04x PID=%04x manufacturer=\"%s\" product=\"%s\" serial=\"%s\"",
             device->info.vid, device->info.pid, device->info.manufacturer, device->info.product, device->info.serial);
    device->vendorSerialSupported = isKnownVendorSerial(device->info.vid, device->info.pid);
    if (device->vendorSerialSupported)
    {
      ESP_LOGI(TAG, "%s VCP candidate detected: VID=%04x PID=%04x",
               vendorSerialName(device->info.vid),
               device->info.vid,
               device->info.pid);
    }
  }

  const usb_config_desc_t *configDesc = nullptr;
  err = usb_host_get_active_config_descriptor(device->handle, &configDesc);
  if (err != ESP_OK || !configDesc)
  {
    ESP_LOGE(TAG, "usb_host_get_active_config_descriptor() failed: %s", esp_err_to_name(err));
    setLastError(err);
    usb_host_device_close(clientHandle_, device->handle);
    *device = DeviceState();
    return;
  }
  device->info.configurationValue = configDesc->bConfigurationValue;
  device->info.configurationAttributes = configDesc->bmAttributes;
  device->info.configurationMaxPower = configDesc->bMaxPower;
  device->info.configurationInterfaceCount = configDesc->bNumInterfaces;
  device->info.configurationTotalLength = configDesc->wTotalLength;

  const bool isHub = devDesc && (devDesc->bDeviceClass == USB_CLASS_HUB_VALUE || configHasInterfaceClass(configDesc, USB_CLASS_HUB_VALUE));
  device->isHub = isHub;
  if (isHub)
  {
    if (nextHubIndex_ <= 0x0f)
    {
      device->hubIndex = nextHubIndex_++;
    }
    else
    {
      ESP_LOGW(TAG, "Hub index exhausted for address=%u", address);
    }
  }
  const bool hasHid = configHasInterfaceClass(configDesc, USB_CLASS_HID_VALUE);
  const bool hasCdc = configHasInterfaceClass(configDesc, USB_CLASS_CDC_CONTROL_VALUE) || configHasInterfaceClass(configDesc, USB_CLASS_CDC_DATA_VALUE);
  const bool hasAudio = configHasInterfaceClass(configDesc, USB_CLASS_AUDIO_VALUE);
  if (!isHub && !hasHid && !hasCdc && !hasAudio && !device->vendorSerialSupported)
  {
    ESP_LOGI(TAG, "Ignoring unsupported device at address=%u", address);
    usb_host_device_close(clientHandle_, device->handle);
    *device = DeviceState();
    return;
  }

  parseConfigDescriptor(*device, configDesc);
  if (deviceConnectedCallback_)
  {
    deviceConnectedCallback_(device->info);
  }
}

void EspUsbHost::handleDeviceGone(usb_device_handle_t goneHandle)
{
  ESP_LOGI(TAG, "Device disconnected");
  DeviceState *device = findDeviceByHandle(goneHandle);
  if (!device)
  {
    if (goneHandle)
    {
      usb_host_device_close(clientHandle_, goneHandle);
    }
    return;
  }

  EspUsbHostDeviceInfo info = device->info;
  releaseEndpoints(*device, false);
  releaseInterfaces(*device);
  if (device->handle)
  {
    usb_host_device_close(clientHandle_, device->handle);
  }

  if (deviceDisconnectedCallback_)
  {
    deviceDisconnectedCallback_(info);
  }
  *device = DeviceState();
}

void EspUsbHost::parseConfigDescriptor(DeviceState &device, const usb_config_desc_t *configDesc)
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
    currentDevice_ = &device;
    handleDescriptor(p[i + 1], &p[i]);
    i += length;
  }
  currentDevice_ = nullptr;
}

void EspUsbHost::handleDescriptor(uint8_t descriptorType, const uint8_t *data)
{
  DeviceState *device = currentDevice_;
  if (!device)
  {
    return;
  }

  switch (descriptorType)
  {
  case USB_INTERFACE_DESC:
  {
    const usb_intf_desc_t *intf = reinterpret_cast<const usb_intf_desc_t *>(data);
    currentInterfaceNumber_ = intf->bInterfaceNumber;
    currentInterfaceAlternate_ = intf->bAlternateSetting;
    currentInterfaceClass_ = intf->bInterfaceClass;
    currentInterfaceSubClass_ = intf->bInterfaceSubClass;
    currentInterfaceProtocol_ = intf->bInterfaceProtocol;
    currentAudioChannels_ = 0;
    currentAudioBytesPerSample_ = 0;
    currentAudioBitsPerSample_ = 0;
    currentAudioSampleRate_ = 0;
    currentClaimResult_ = ESP_OK;
    if (device->interfaceInfoCount < ESP_USB_HOST_MAX_INTERFACES)
    {
      EspUsbHostInterfaceInfo &info = device->interfaceInfos[device->interfaceInfoCount++];
      info.number = intf->bInterfaceNumber;
      info.alternate = intf->bAlternateSetting;
      info.interfaceClass = intf->bInterfaceClass;
      info.interfaceSubClass = intf->bInterfaceSubClass;
      info.interfaceProtocol = intf->bInterfaceProtocol;
      info.endpointCount = intf->bNumEndpoints;
    }

    ESP_LOGI(TAG, "Interface %u class=0x%02x subclass=0x%02x protocol=0x%02x endpoints=%u",
             currentInterfaceNumber_, currentInterfaceClass_, currentInterfaceSubClass_,
             currentInterfaceProtocol_, intf->bNumEndpoints);

    const bool isVendorSerialInterface = device->vendorSerialSupported &&
                                         currentInterfaceClass_ == USB_CLASS_VENDOR_VALUE &&
                                         !device->hasVendorSerialInterface;
    const bool isMidiInterface = currentInterfaceClass_ == USB_CLASS_AUDIO_VALUE &&
                                 currentInterfaceSubClass_ == USB_AUDIO_SUBCLASS_MIDI_STREAMING &&
                                 !device->hasMidiInterface;
    bool interfaceAlreadyClaimed = false;
    for (uint8_t i = 0; i < device->interfaceCount; i++)
    {
      if (device->interfaces[i] == currentInterfaceNumber_)
      {
        interfaceAlreadyClaimed = true;
        break;
      }
    }
    const bool isAudioInterface = currentInterfaceClass_ == USB_CLASS_AUDIO_VALUE &&
                                  currentInterfaceSubClass_ == USB_AUDIO_SUBCLASS_AUDIO_STREAMING &&
                                  intf->bNumEndpoints > 0 &&
                                  !interfaceAlreadyClaimed;
    if (currentInterfaceClass_ == USB_CLASS_HID_VALUE ||
        currentInterfaceClass_ == USB_CLASS_CDC_CONTROL_VALUE ||
        currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE ||
        isAudioInterface ||
        isMidiInterface ||
        isVendorSerialInterface)
    {
      currentClaimResult_ = usb_host_interface_claim(clientHandle_, device->handle, currentInterfaceNumber_, intf->bAlternateSetting);
      if (currentClaimResult_ == ESP_OK && device->interfaceCount < sizeof(device->interfaces))
      {
        device->interfaces[device->interfaceCount++] = currentInterfaceNumber_;
        ESP_LOGI(TAG, "Interface %u claimed", currentInterfaceNumber_);
        if (currentInterfaceAlternate_ > 0 && !isAudioInterface)
        {
          submitSetInterface(*device, currentInterfaceNumber_, currentInterfaceAlternate_);
        }
        if (currentInterfaceSubClass_ == HID_SUBCLASS_BOOT_VALUE &&
            currentInterfaceProtocol_ == HID_PROTOCOL_KEYBOARD_VALUE &&
            !device->hasKeyboardInterface)
        {
          device->hasKeyboardInterface = true;
          device->keyboardInterfaceNumber = currentInterfaceNumber_;
          ESP_LOGI(TAG, "Keyboard interface ready: iface=%u", device->keyboardInterfaceNumber);
        }
        if (currentInterfaceClass_ == USB_CLASS_CDC_CONTROL_VALUE)
        {
          device->hasCdcControlInterface = true;
          device->cdcControlInterfaceNumber = currentInterfaceNumber_;
          ESP_LOGI(TAG, "CDC control interface ready: iface=%u", device->cdcControlInterfaceNumber);
          configureCdcAcm(*device);
        }
        else if (currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE)
        {
          device->hasCdcDataInterface = true;
          device->cdcDataInterfaceNumber = currentInterfaceNumber_;
          ESP_LOGI(TAG, "CDC data interface ready: iface=%u", device->cdcDataInterfaceNumber);
        }
        else if (isVendorSerialInterface)
        {
          device->hasVendorSerialInterface = true;
          device->vendorSerialInterfaceNumber = currentInterfaceNumber_;
          ESP_LOGI(TAG, "%s VCP interface ready: iface=%u",
                   vendorSerialName(device->info.vid),
                   device->vendorSerialInterfaceNumber);
          configureVendorSerial(*device);
        }
        else if (isMidiInterface)
        {
          device->hasMidiInterface = true;
          device->midiInterfaceNumber = currentInterfaceNumber_;
          ESP_LOGI(TAG, "USB MIDI interface ready: iface=%u", device->midiInterfaceNumber);
        }
        else if (isAudioInterface)
        {
          device->hasAudioInterface = true;
          device->audioInterfaceNumber = currentInterfaceNumber_;
          ESP_LOGI(TAG, "USB Audio streaming interface ready: iface=%u alt=%u",
                   device->audioInterfaceNumber,
                   intf->bAlternateSetting);
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

  case USB_CS_INTERFACE_DESC:
  {
    if (currentInterfaceClass_ == USB_CLASS_AUDIO_VALUE &&
        currentInterfaceSubClass_ == USB_AUDIO_SUBCLASS_AUDIO_STREAMING &&
        data[0] >= 8 &&
        data[2] == USB_AUDIO_CS_AS_FORMAT_TYPE &&
        data[3] == USB_AUDIO_FORMAT_TYPE_I)
    {
      currentAudioChannels_ = data[4];
      currentAudioBytesPerSample_ = data[5];
      currentAudioBitsPerSample_ = data[6];
      const uint8_t sampleFrequencyType = data[7];
      if (sampleFrequencyType > 0 && data[0] >= 11)
      {
        currentAudioSampleRate_ = static_cast<uint32_t>(data[8]) |
                                  (static_cast<uint32_t>(data[9]) << 8) |
                                  (static_cast<uint32_t>(data[10]) << 16);
      }
      ESP_LOGI(TAG, "USB Audio Type I format: iface=%u channels=%u bytes=%u bits=%u rate=%lu",
               currentInterfaceNumber_,
               currentAudioChannels_,
               currentAudioBytesPerSample_,
               currentAudioBitsPerSample_,
               static_cast<unsigned long>(currentAudioSampleRate_));
    }
    break;
  }

  case USB_ENDPOINT_DESC:
  {
    const usb_ep_desc_t *ep = reinterpret_cast<const usb_ep_desc_t *>(data);
    if (device->endpointInfoCount < ESP_USB_HOST_MAX_ENDPOINTS)
    {
      EspUsbHostEndpointInfo &info = device->endpointInfos[device->endpointInfoCount++];
      info.address = ep->bEndpointAddress;
      info.interfaceNumber = currentInterfaceNumber_;
      info.attributes = ep->bmAttributes;
      info.maxPacketSize = ep->wMaxPacketSize;
      info.interval = ep->bInterval;
    }
    const bool isIn = (ep->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0;
    const bool isInterrupt = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_INT;
    const bool isBulk = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_BULK;
    const bool isIsochronous = (ep->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_ISOC;

    const bool isSerialBulkEndpoint = currentClaimResult_ == ESP_OK &&
                                       (currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE ||
                                       (device->vendorSerialSupported && currentInterfaceClass_ == USB_CLASS_VENDOR_VALUE)) &&
                                      isBulk;
    if (isSerialBulkEndpoint)
    {
      if (!isIn)
      {
        device->hasSerialOutEndpoint = true;
        device->serialOutEndpointAddress = ep->bEndpointAddress;
        device->serialOutPacketSize = ep->wMaxPacketSize;
        ESP_LOGI(TAG, "%s bulk OUT endpoint ready: iface=%u ep=0x%02x size=%u",
                 currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE ? "CDC" : vendorSerialName(device->info.vid),
                 currentInterfaceNumber_,
                 ep->bEndpointAddress,
                 ep->wMaxPacketSize);
        return;
      }

      EndpointState *endpoint = allocateEndpoint(*device);
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
      endpoint->transfer->device_handle = device->handle;
      endpoint->transfer->bEndpointAddress = ep->bEndpointAddress;
      endpoint->transfer->callback = transferCallback;
      endpoint->transfer->context = this;
      endpoint->transfer->num_bytes = ep->wMaxPacketSize;

      if (submitInputTransfer(*endpoint))
      {
        ESP_LOGI(TAG, "%s bulk IN endpoint ready: iface=%u ep=0x%02x size=%u",
                 currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE ? "CDC" : vendorSerialName(device->info.vid),
                 endpoint->interfaceNumber,
                 endpoint->address,
                 ep->wMaxPacketSize);
      }
      return;
    }

    if (currentClaimResult_ == ESP_OK &&
        currentInterfaceClass_ == USB_CLASS_AUDIO_VALUE &&
        currentInterfaceSubClass_ == USB_AUDIO_SUBCLASS_MIDI_STREAMING &&
        isBulk)
    {
      if (!isIn)
      {
        device->hasMidiOutEndpoint = true;
        device->midiOutEndpointAddress = ep->bEndpointAddress;
        device->midiOutPacketSize = ep->wMaxPacketSize;
        ESP_LOGI(TAG, "USB MIDI bulk OUT endpoint ready: iface=%u ep=0x%02x size=%u",
                 currentInterfaceNumber_,
                 ep->bEndpointAddress,
                 ep->wMaxPacketSize);
        return;
      }

      EndpointState *endpoint = allocateEndpoint(*device);
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
        ESP_LOGW(TAG, "usb_host_transfer_alloc(MIDI IN) failed: %s", esp_err_to_name(err));
        setLastError(err);
        return;
      }

      endpoint->address = ep->bEndpointAddress;
      endpoint->interfaceNumber = currentInterfaceNumber_;
      endpoint->interfaceClass = currentInterfaceClass_;
      endpoint->interfaceSubClass = currentInterfaceSubClass_;
      endpoint->interfaceProtocol = currentInterfaceProtocol_;
      endpoint->transfer->device_handle = device->handle;
      endpoint->transfer->bEndpointAddress = ep->bEndpointAddress;
      endpoint->transfer->callback = transferCallback;
      endpoint->transfer->context = this;
      endpoint->transfer->num_bytes = ep->wMaxPacketSize;

      if (submitInputTransfer(*endpoint))
      {
        ESP_LOGI(TAG, "USB MIDI bulk IN endpoint ready: iface=%u ep=0x%02x size=%u",
                 endpoint->interfaceNumber,
                 endpoint->address,
                 ep->wMaxPacketSize);
      }
      return;
    }

    if (currentClaimResult_ == ESP_OK &&
        currentInterfaceClass_ == USB_CLASS_AUDIO_VALUE &&
        currentInterfaceSubClass_ == USB_AUDIO_SUBCLASS_AUDIO_STREAMING &&
        isIsochronous)
    {
      static constexpr int AUDIO_ISOC_PACKETS = 8;
      if (!isIn)
      {
        recordAudioStream(*device, ep, false);
        device->hasAudioInterface = true;
        device->audioInterfaceNumber = currentInterfaceNumber_;
        device->hasAudioOutEndpoint = true;
        device->audioOutInterfaceNumber = currentInterfaceNumber_;
        device->audioOutEndpointAddress = ep->bEndpointAddress;
        device->audioOutPacketSize = ep->wMaxPacketSize;
        if (currentInterfaceAlternate_ > 0)
        {
          submitAudioSamplingFrequency(*device, ep->bEndpointAddress, device->audioSampleRate);
          submitSetInterface(*device, currentInterfaceNumber_, currentInterfaceAlternate_);
        }
        ESP_LOGI(TAG, "USB Audio isochronous OUT endpoint ready: iface=%u ep=0x%02x size=%u interval=%u",
                 currentInterfaceNumber_,
                 ep->bEndpointAddress,
                 ep->wMaxPacketSize,
                 ep->bInterval);
        return;
      }

      recordAudioStream(*device, ep, true);
      EndpointState *endpoint = allocateEndpoint(*device);
      if (!endpoint)
      {
        ESP_LOGW(TAG, "No endpoint slots available");
        setLastError(ESP_ERR_NO_MEM);
        return;
      }

      const size_t transferSize = static_cast<size_t>(ep->wMaxPacketSize) * AUDIO_ISOC_PACKETS;
      esp_err_t err = usb_host_transfer_alloc(transferSize, AUDIO_ISOC_PACKETS, &endpoint->transfer);
      if (err != ESP_OK)
      {
        endpoint->inUse = false;
        ESP_LOGW(TAG, "usb_host_transfer_alloc(audio IN) failed: %s", esp_err_to_name(err));
        setLastError(err);
        return;
      }

      endpoint->address = ep->bEndpointAddress;
      endpoint->interfaceNumber = currentInterfaceNumber_;
      endpoint->interfaceClass = currentInterfaceClass_;
      endpoint->interfaceSubClass = currentInterfaceSubClass_;
      endpoint->interfaceProtocol = currentInterfaceProtocol_;
      endpoint->transfer->device_handle = device->handle;
      endpoint->transfer->bEndpointAddress = ep->bEndpointAddress;
      endpoint->transfer->callback = transferCallback;
      endpoint->transfer->context = this;
      endpoint->transfer->num_bytes = transferSize;
      for (int i = 0; i < endpoint->transfer->num_isoc_packets; i++)
      {
        endpoint->transfer->isoc_packet_desc[i].num_bytes = ep->wMaxPacketSize;
        endpoint->transfer->isoc_packet_desc[i].actual_num_bytes = 0;
        endpoint->transfer->isoc_packet_desc[i].status = USB_TRANSFER_STATUS_COMPLETED;
      }

      if (currentInterfaceAlternate_ == 0)
      {
        submitInputTransfer(*endpoint);
      }
      else
      {
        submitAudioSamplingFrequency(*device, ep->bEndpointAddress, device->audioSampleRate);
        submitSetInterface(*device, currentInterfaceNumber_, currentInterfaceAlternate_);
      }
      ESP_LOGI(TAG, "USB Audio isochronous IN endpoint ready: iface=%u ep=0x%02x size=%u interval=%u",
               endpoint->interfaceNumber,
               endpoint->address,
               ep->wMaxPacketSize,
               ep->bInterval);
      return;
    }

    if (currentClaimResult_ == ESP_OK &&
        currentInterfaceClass_ == USB_CLASS_HID_VALUE &&
        !isIn &&
        isInterrupt)
    {
      device->hasVendorInterface = true;
      device->hasVendorOutEndpoint = true;
      device->vendorInterfaceNumber = currentInterfaceNumber_;
      device->vendorOutEndpointAddress = ep->bEndpointAddress;
      device->vendorOutPacketSize = ep->wMaxPacketSize;
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

    EndpointState *endpoint = allocateEndpoint(*device);
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
    endpoint->transfer->device_handle = device->handle;
    endpoint->transfer->bEndpointAddress = ep->bEndpointAddress;
    endpoint->transfer->callback = transferCallback;
    endpoint->transfer->context = this;
    endpoint->transfer->num_bytes = ep->wMaxPacketSize;

    if (submitInputTransfer(*endpoint))
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

void EspUsbHost::recordAudioStream(DeviceState &device, const usb_ep_desc_t *ep, bool input)
{
  if (!ep || device.audioStreamInfoCount >= ESP_USB_HOST_MAX_AUDIO_STREAMS)
  {
    return;
  }

  EspUsbHostAudioStreamInfo &info = device.audioStreamInfos[device.audioStreamInfoCount++];
  info.address = device.info.address;
  info.interfaceNumber = currentInterfaceNumber_;
  info.alternate = currentInterfaceAlternate_;
  info.endpointAddress = ep->bEndpointAddress;
  info.input = input;
  info.output = !input;
  info.channels = currentAudioChannels_;
  info.bytesPerSample = currentAudioBytesPerSample_;
  info.bitsPerSample = currentAudioBitsPerSample_;
  info.sampleRate = currentAudioSampleRate_;
  info.maxPacketSize = ep->wMaxPacketSize;
  info.interval = ep->bInterval;
}

void EspUsbHost::transferCallback(usb_transfer_t *transfer)
{
  static_cast<EspUsbHost *>(transfer->context)->handleTransfer(transfer);
}

bool EspUsbHost::submitInputTransfer(EndpointState &endpoint)
{
  if (!endpoint.transfer || endpoint.transferSubmitted)
  {
    return endpoint.transferSubmitted;
  }

  if (endpoint.transfer->num_isoc_packets > 0)
  {
    endpoint.transfer->num_bytes = 0;
    for (int i = 0; i < endpoint.transfer->num_isoc_packets; i++)
    {
      endpoint.transfer->num_bytes += endpoint.transfer->isoc_packet_desc[i].num_bytes;
      endpoint.transfer->isoc_packet_desc[i].actual_num_bytes = 0;
      endpoint.transfer->isoc_packet_desc[i].status = USB_TRANSFER_STATUS_COMPLETED;
    }
  }

  esp_err_t err = usb_host_transfer_submit(endpoint.transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit(ep=0x%02x) failed: %s",
             endpoint.address,
             esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  endpoint.transferSubmitted = true;
  return true;
}

void EspUsbHost::submitPendingTransfers(usb_device_handle_t deviceHandle, uint8_t interfaceNumber)
{
  for (EndpointState &endpoint : endpoints_)
  {
    if (!endpoint.inUse ||
        endpoint.deviceHandle != deviceHandle ||
        endpoint.interfaceNumber != interfaceNumber ||
        endpoint.transferSubmitted)
    {
      continue;
    }

    submitInputTransfer(endpoint);
  }
}

void EspUsbHost::controlTransferCallback(usb_transfer_t *transfer)
{
  EspUsbHost *host = static_cast<EspUsbHost *>(transfer->context);
  usb_device_handle_t deviceHandle = transfer->device_handle;
  uint8_t setInterfaceNumber = 0;
  bool isSetInterface = false;
  if (transfer->data_buffer && transfer->num_bytes >= USB_SETUP_PACKET_SIZE)
  {
    const usb_setup_packet_t *setup = reinterpret_cast<const usb_setup_packet_t *>(transfer->data_buffer);
    static constexpr uint8_t USB_REQUEST_SET_INTERFACE = 0x0b;
    static constexpr uint8_t USB_STANDARD_REQUEST_TYPE = 0x01;
    isSetInterface = (setup->bRequest == USB_REQUEST_SET_INTERFACE &&
                      setup->bmRequestType == USB_STANDARD_REQUEST_TYPE);
    setInterfaceNumber = setup->wIndex & 0xff;
  }

  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED)
  {
    if (host && isSetInterface)
    {
      host->submitPendingTransfers(deviceHandle, setInterfaceNumber);
    }
    if (host && transfer->data_buffer && transfer->num_bytes >= USB_SETUP_PACKET_SIZE)
    {
      const usb_setup_packet_t *setup = reinterpret_cast<const usb_setup_packet_t *>(transfer->data_buffer);
      if (setup->bRequest == HID_CLASS_REQUEST_SET_PROTOCOL &&
          setup->bmRequestType == HID_SET_REPORT_REQUEST_TYPE)
      {
        for (DeviceState &device : host->devices_)
        {
          if (device.inUse && device.handle == deviceHandle &&
              device.hasKeyboardInterface &&
              device.keyboardInterfaceNumber == (setup->wIndex & 0xff))
          {
            device.keyboardReportProtocolSet = true;
            if (device.keyboardLedPending)
            {
              device.keyboardLedPending = false;
              host->sendKeyboardLedReport(device, device.keyboardLedPendingMask);
            }
            break;
          }
        }
      }
    }
  }
  else
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
  EndpointState *endpoint = findEndpoint(transfer->device_handle, transfer->bEndpointAddress);
  if (!endpoint)
  {
    return;
  }
  DeviceState *device = findDeviceByHandle(endpoint->deviceHandle);

  const bool isAudioStreaming = endpoint->interfaceClass == USB_CLASS_AUDIO_VALUE &&
                                endpoint->interfaceSubClass == USB_AUDIO_SUBCLASS_AUDIO_STREAMING;
  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED &&
      (transfer->actual_num_bytes > 0 || isAudioStreaming))
  {
#if CORE_DEBUG_LEVEL >= ARDUHAL_LOG_LEVEL_VERBOSE
    const char *transferKind = "input";
    if (endpoint->interfaceClass == USB_CLASS_HID_VALUE)
    {
      transferKind = "HID input";
    }
    else if (endpoint->interfaceClass == USB_CLASS_CDC_DATA_VALUE)
    {
      transferKind = "CDC serial input";
    }
    else if (endpoint->interfaceClass == USB_CLASS_AUDIO_VALUE &&
             endpoint->interfaceSubClass == USB_AUDIO_SUBCLASS_MIDI_STREAMING)
    {
      transferKind = "USB MIDI input";
    }
    else if (endpoint->interfaceClass == USB_CLASS_AUDIO_VALUE &&
             endpoint->interfaceSubClass == USB_AUDIO_SUBCLASS_AUDIO_STREAMING)
    {
      transferKind = "USB Audio input";
    }
    else if (device && device->vendorSerialSupported && endpoint->interfaceClass == USB_CLASS_VENDOR_VALUE)
    {
      transferKind = "VCP serial input";
    }
    ESP_LOGV(TAG, "%s iface=%u ep=0x%02x len=%u",
             transferKind,
             endpoint->interfaceNumber,
             transfer->bEndpointAddress,
             transfer->actual_num_bytes);
#endif

    if (endpoint->interfaceClass == USB_CLASS_HID_VALUE && hidInputCallback_)
    {
      EspUsbHostHIDInput input;
      input.address = endpoint->deviceAddress;
      input.interfaceNumber = endpoint->interfaceNumber;
      input.subclass = endpoint->interfaceSubClass;
      input.protocol = endpoint->interfaceProtocol;
      input.data = transfer->data_buffer;
      input.length = transfer->actual_num_bytes;
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
             (device && device->vendorSerialSupported && endpoint->interfaceClass == USB_CLASS_VENDOR_VALUE))
    {
      handleSerial(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
    }
    else if (endpoint->interfaceClass == USB_CLASS_AUDIO_VALUE &&
             endpoint->interfaceSubClass == USB_AUDIO_SUBCLASS_MIDI_STREAMING)
    {
      handleMidi(*endpoint, transfer->data_buffer, transfer->actual_num_bytes);
    }
    else if (isAudioStreaming)
    {
      handleAudio(*endpoint, transfer);
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

  if (running_ && endpoint->deviceHandle)
  {
    if (transfer->num_isoc_packets > 0)
    {
      transfer->num_bytes = 0;
      for (int i = 0; i < transfer->num_isoc_packets; i++)
      {
        transfer->num_bytes += transfer->isoc_packet_desc[i].num_bytes;
        transfer->isoc_packet_desc[i].actual_num_bytes = 0;
        transfer->isoc_packet_desc[i].status = USB_TRANSFER_STATUS_COMPLETED;
      }
    }
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
    events[i].address = endpoint.deviceAddress;
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
  event.address = endpoint.deviceAddress;

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

  if (cdcSerial_ && cdcSerial_->accepts(endpoint.deviceAddress))
  {
    cdcSerial_->pushData(data, length);
  }

  if (!serialDataCallback_)
  {
    return;
  }

  EspUsbHostSerialData serial;
  serial.address = endpoint.deviceAddress;
  serial.interfaceNumber = endpoint.interfaceNumber;
  serial.data = data;
  serial.length = length;
  serialDataCallback_(serial);
}

void EspUsbHost::handleMidi(EndpointState &endpoint, const uint8_t *data, size_t length)
{
  if (!midiMessageCallback_ || !data || length < 4)
  {
    return;
  }

  for (size_t offset = 0; offset + 3 < length; offset += 4)
  {
    const uint8_t *packet = data + offset;
    EspUsbHostMidiMessage message;
    message.address = endpoint.deviceAddress;
    message.interfaceNumber = endpoint.interfaceNumber;
    message.cable = packet[0] >> 4;
    message.codeIndex = packet[0] & 0x0f;
    message.status = packet[1];
    message.data1 = packet[2];
    message.data2 = packet[3];
    message.raw = packet;
    message.length = 4;

    ESP_LOGD(TAG, "MIDI iface=%u cable=%u cin=0x%02x status=0x%02x data1=0x%02x data2=0x%02x",
             message.interfaceNumber,
             message.cable,
             message.codeIndex,
             message.status,
             message.data1,
             message.data2);
    midiMessageCallback_(message);
  }
}

void EspUsbHost::handleAudio(EndpointState &endpoint, usb_transfer_t *transfer)
{
  if (!audioDataCallback_ || !transfer || !transfer->data_buffer || transfer->num_isoc_packets <= 0)
  {
    return;
  }

  size_t offset = 0;
  for (int i = 0; i < transfer->num_isoc_packets; i++)
  {
    const usb_isoc_packet_desc_t &packet = transfer->isoc_packet_desc[i];
    if (packet.status == USB_TRANSFER_STATUS_COMPLETED && packet.actual_num_bytes > 0)
    {
      EspUsbHostAudioData audio;
      audio.address = endpoint.deviceAddress;
      audio.interfaceNumber = endpoint.interfaceNumber;
      audio.data = transfer->data_buffer + offset;
      audio.length = packet.actual_num_bytes;
      audioDataCallback_(audio);
    }
    offset += packet.num_bytes;
  }
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
  event.address = endpoint.deviceAddress;

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
                                    endpoint.lastGamepadState,
                                    event))
  {
    return;
  }
  event.address = endpoint.deviceAddress;

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
  endpoint.lastGamepadState = {event.x, event.y, event.z, event.rz, event.rx, event.ry, event.hat, event.buttons};
}

void EspUsbHost::handleVendorInput(EndpointState &endpoint, const uint8_t *data, size_t length)
{
  DeviceState *device = findDeviceByHandle(endpoint.deviceHandle);
  if (device && !device->hasVendorInterface)
  {
    device->hasVendorInterface = true;
    device->vendorInterfaceNumber = endpoint.interfaceNumber;
    ESP_LOGI(TAG, "Vendor HID interface ready: address=%u iface=%u", device->info.address, device->vendorInterfaceNumber);
  }

  if (!vendorInputCallback_)
  {
    return;
  }

  EspUsbHostVendorInput input;
  input.address = endpoint.deviceAddress;
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
  event.address = endpoint.deviceAddress;

  ESP_LOGD(TAG, "SystemControl iface=%u usage=0x%02x pressed=%u released=%u",
           event.interfaceNumber,
           event.usage,
           event.pressed ? 1 : 0,
           event.released ? 1 : 0);
  systemControlCallback_(event);
  endpoint.lastSystemUsage = event.pressed ? event.usage : 0;
}

EspUsbHost::EndpointState *EspUsbHost::findEndpoint(usb_device_handle_t deviceHandle, uint8_t endpointAddress)
{
  for (EndpointState &endpoint : endpoints_)
  {
    if (endpoint.inUse && endpoint.deviceHandle == deviceHandle && endpoint.address == endpointAddress)
    {
      return &endpoint;
    }
  }
  return nullptr;
}

EspUsbHost::EndpointState *EspUsbHost::allocateEndpoint(DeviceState &device)
{
  for (EndpointState &endpoint : endpoints_)
  {
    if (!endpoint.inUse)
    {
      endpoint = EndpointState();
      endpoint.inUse = true;
      endpoint.deviceIndex = static_cast<uint8_t>(&device - devices_);
      endpoint.deviceAddress = device.info.address;
      endpoint.deviceHandle = device.handle;
      return &endpoint;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::allocateDevice()
{
  for (DeviceState &device : devices_)
  {
    if (!device.inUse)
    {
      device = DeviceState();
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findDevice(uint8_t address)
{
  return const_cast<DeviceState *>(static_cast<const EspUsbHost *>(this)->findDevice(address));
}

const EspUsbHost::DeviceState *EspUsbHost::findDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findDeviceByHandle(usb_device_handle_t handle)
{
  for (DeviceState &device : devices_)
  {
    if (device.inUse && device.handle == handle)
    {
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findSerialDevice(uint8_t address)
{
  return const_cast<DeviceState *>(static_cast<const EspUsbHost *>(this)->findSerialDevice(address));
}

const EspUsbHost::DeviceState *EspUsbHost::findSerialDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || !device.hasSerialOutEndpoint)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findMidiDevice(uint8_t address)
{
  return const_cast<DeviceState *>(static_cast<const EspUsbHost *>(this)->findMidiDevice(address));
}

const EspUsbHost::DeviceState *EspUsbHost::findMidiDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || !device.hasMidiOutEndpoint)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findAudioOutputDevice(uint8_t address)
{
  return const_cast<DeviceState *>(static_cast<const EspUsbHost *>(this)->findAudioOutputDevice(address));
}

const EspUsbHost::DeviceState *EspUsbHost::findAudioOutputDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || !device.hasAudioOutEndpoint)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

const EspUsbHost::DeviceState *EspUsbHost::findAudioDevice(uint8_t address) const
{
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse || !device.hasAudioInterface)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findKeyboardDevice(uint8_t address)
{
  for (DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || !device.hasKeyboardInterface)
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

EspUsbHost::DeviceState *EspUsbHost::findVendorDevice(uint8_t address)
{
  for (DeviceState &device : devices_)
  {
    if (!device.inUse || !device.handle || (!device.hasVendorInterface && !device.hasVendorOutEndpoint))
    {
      continue;
    }
    if (address == ESP_USB_HOST_ANY_ADDRESS || device.info.address == address)
    {
      return &device;
    }
  }
  return nullptr;
}

size_t EspUsbHost::deviceCount() const
{
  size_t count = 0;
  for (const DeviceState &device : devices_)
  {
    if (device.inUse)
    {
      count++;
    }
  }
  return count;
}

size_t EspUsbHost::getDevices(EspUsbHostDeviceInfo *devices, size_t maxDevices) const
{
  if (!devices || maxDevices == 0)
  {
    return 0;
  }

  size_t count = 0;
  for (const DeviceState &device : devices_)
  {
    if (!device.inUse)
    {
      continue;
    }
    devices[count++] = device.info;
    if (count >= maxDevices)
    {
      break;
    }
  }
  return count;
}

bool EspUsbHost::getDevice(uint8_t address, EspUsbHostDeviceInfo &deviceInfo) const
{
  const DeviceState *device = findDevice(address);
  if (!device)
  {
    return false;
  }
  deviceInfo = device->info;
  return true;
}

size_t EspUsbHost::getInterfaces(uint8_t address, EspUsbHostInterfaceInfo *interfaces, size_t maxInterfaces) const
{
  if (!interfaces || maxInterfaces == 0)
  {
    return 0;
  }

  const DeviceState *device = findDevice(address);
  if (!device)
  {
    return 0;
  }

  const size_t count = device->interfaceInfoCount < maxInterfaces ? device->interfaceInfoCount : maxInterfaces;
  for (size_t i = 0; i < count; i++)
  {
    interfaces[i] = device->interfaceInfos[i];
  }
  return count;
}

size_t EspUsbHost::getEndpoints(uint8_t address, EspUsbHostEndpointInfo *endpoints, size_t maxEndpoints) const
{
  if (!endpoints || maxEndpoints == 0)
  {
    return 0;
  }

  const DeviceState *device = findDevice(address);
  if (!device)
  {
    return 0;
  }

  const size_t count = device->endpointInfoCount < maxEndpoints ? device->endpointInfoCount : maxEndpoints;
  for (size_t i = 0; i < count; i++)
  {
    endpoints[i] = device->endpointInfos[i];
  }
  return count;
}

size_t EspUsbHost::getAudioStreams(uint8_t address, EspUsbHostAudioStreamInfo *streams, size_t maxStreams) const
{
  if (!streams || maxStreams == 0)
  {
    return 0;
  }

  const DeviceState *device = findDevice(address);
  if (!device)
  {
    return 0;
  }

  const size_t count = device->audioStreamInfoCount < maxStreams ? device->audioStreamInfoCount : maxStreams;
  for (size_t i = 0; i < count; i++)
  {
    streams[i] = device->audioStreamInfos[i];
  }
  return count;
}

void EspUsbHost::releaseEndpoints(DeviceState &device, bool clearEndpoints)
{
  for (EndpointState &endpoint : endpoints_)
  {
    if (!endpoint.inUse || endpoint.deviceHandle != device.handle)
    {
      continue;
    }
    if (endpoint.transfer)
    {
      if (clearEndpoints && device.handle)
      {
        esp_err_t err = usb_host_endpoint_clear(device.handle, endpoint.address);
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

void EspUsbHost::releaseAllEndpoints(bool clearEndpoints)
{
  for (DeviceState &device : devices_)
  {
    if (device.inUse)
    {
      releaseEndpoints(device, clearEndpoints);
    }
  }
}

void EspUsbHost::releaseInterfaces(DeviceState &device)
{
  for (uint8_t i = 0; i < device.interfaceCount; i++)
  {
    usb_host_interface_release(clientHandle_, device.handle, device.interfaces[i]);
    device.interfaces[i] = 0;
  }
  device.interfaceCount = 0;
}

void EspUsbHost::configureCdcAcm(DeviceState &device)
{
  if (device.cdcConfigured || !device.hasCdcControlInterface || !device.handle || !clientHandle_)
  {
    return;
  }

  uint8_t lineCoding[7] = {
      static_cast<uint8_t>(device.serialBaudRate & 0xff),
      static_cast<uint8_t>((device.serialBaudRate >> 8) & 0xff),
      static_cast<uint8_t>((device.serialBaudRate >> 16) & 0xff),
      static_cast<uint8_t>((device.serialBaudRate >> 24) & 0xff),
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
    setup->wIndex = device.cdcControlInterfaceNumber;
    setup->wLength = sizeof(lineCoding);
    memcpy(lineCodingTransfer->data_buffer + USB_SETUP_PACKET_SIZE, lineCoding, sizeof(lineCoding));
    lineCodingTransfer->device_handle = device.handle;
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
    setup->wValue = (device.serialDtr ? 0x0001 : 0) | (device.serialRts ? 0x0002 : 0);
    setup->wIndex = device.cdcControlInterfaceNumber;
    setup->wLength = 0;
    lineStateTransfer->device_handle = device.handle;
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

  device.cdcConfigured = true;
  ESP_LOGI(TAG, "CDC ACM configured: baud=%lu dtr=%u rts=%u",
           static_cast<unsigned long>(device.serialBaudRate),
           device.serialDtr ? 1 : 0,
           device.serialRts ? 1 : 0);
}

void EspUsbHost::attachCdcSerial(EspUsbHostCdcSerial *serial)
{
  cdcSerial_ = serial;
}

void EspUsbHost::configureVendorSerial(DeviceState &device)
{
  if (!device.vendorSerialSupported || !device.hasVendorSerialInterface || !device.handle || !clientHandle_)
  {
    return;
  }

  ESP_LOGI(TAG, "Configuring %s VCP: iface=%u baud=%lu dtr=%u rts=%u",
           vendorSerialName(device.info.vid),
           device.vendorSerialInterfaceNumber,
           static_cast<unsigned long>(device.serialBaudRate),
           device.serialDtr ? 1 : 0,
           device.serialRts ? 1 : 0);

  if (device.info.vid == 0x0403)
  {
    uint16_t divisor = 0x001a;
    if (device.serialBaudRate == 9600)
    {
      divisor = 0x4138;
    }
    else if (device.serialBaudRate == 57600)
    {
      divisor = 0x0034;
    }
    else if (device.serialBaudRate == 230400)
    {
      divisor = 0x000d;
    }

    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x00, 0x0000, device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x03, divisor, device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x04, 0x0008, device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x02, device.serialDtr ? 0x0011 : 0x0010, device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x02, device.serialRts ? 0x0021 : 0x0020, device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
  }
  else if (device.info.vid == 0x10c4)
  {
    const uint8_t baud[4] = {
        static_cast<uint8_t>(device.serialBaudRate & 0xff),
        static_cast<uint8_t>((device.serialBaudRate >> 8) & 0xff),
        static_cast<uint8_t>((device.serialBaudRate >> 16) & 0xff),
        static_cast<uint8_t>((device.serialBaudRate >> 24) & 0xff)};
    submitVendorSerialControl(VENDOR_INTERFACE_OUT_REQUEST_TYPE, 0x00, 0x0001, device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_INTERFACE_OUT_REQUEST_TYPE, 0x1e, 0x0000, device.vendorSerialInterfaceNumber, baud, sizeof(baud), device.info.address);
    submitVendorSerialControl(VENDOR_INTERFACE_OUT_REQUEST_TYPE, 0x03, 0x0800, device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_INTERFACE_OUT_REQUEST_TYPE, 0x07,
                              (device.serialDtr ? 0x0001 : 0) | (device.serialRts ? 0x0002 : 0) | 0x0300,
                              device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
  }
  else if (device.info.vid == 0x1a86)
  {
    uint16_t baudReg = 0xd980;
    if (device.serialBaudRate == 9600)
    {
      baudReg = 0xb282;
    }
    else if (device.serialBaudRate == 57600)
    {
      baudReg = 0x9881;
    }
    else if (device.serialBaudRate == 230400)
    {
      baudReg = 0xcc83;
    }

    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0xa1, 0x0000, 0x0000, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x9a, 0x1312, baudReg, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0x9a, 0x2518, 0x00c3, nullptr, 0, device.info.address);
    submitVendorSerialControl(VENDOR_OUT_REQUEST_TYPE, 0xa4,
                              (device.serialDtr ? 0x0020 : 0) | (device.serialRts ? 0x0040 : 0),
                              device.vendorSerialInterfaceNumber, nullptr, 0, device.info.address);
  }
}

bool EspUsbHost::submitSetInterface(DeviceState &device, uint8_t interfaceNumber, uint8_t alternateSetting)
{
  if (!clientHandle_ || !device.handle)
  {
    return false;
  }

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(Set_Interface) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  USB_SETUP_PACKET_INIT_SET_INTERFACE(setup, interfaceNumber, alternateSetting);
  transfer->device_handle = device.handle;
  transfer->bEndpointAddress = 0;
  transfer->callback = controlTransferCallback;
  transfer->context = this;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(Set_Interface iface=%u alt=%u) failed: %s",
             interfaceNumber,
             alternateSetting,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }

  ESP_LOGD(TAG, "Set_Interface submitted iface=%u alt=%u", interfaceNumber, alternateSetting);
  return true;
}

bool EspUsbHost::submitAudioSamplingFrequency(DeviceState &device, uint8_t endpointAddress, uint32_t sampleRate)
{
  if (!clientHandle_ || !device.handle)
  {
    return false;
  }

  static constexpr uint8_t AUDIO_SET_CUR_REQUEST_TYPE = 0x22;
  static constexpr uint8_t AUDIO_SET_CUR_REQUEST = 0x01;
  static constexpr uint16_t AUDIO_EP_SAMPLING_FREQ_CONTROL = 0x0100;
  static constexpr size_t AUDIO_SAMPLE_RATE_LENGTH = 3;

  usb_transfer_t *transfer = nullptr;
  esp_err_t err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + AUDIO_SAMPLE_RATE_LENGTH, 0, &transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_alloc(Audio SET_CUR sampling frequency) failed: %s", esp_err_to_name(err));
    setLastError(err);
    return false;
  }

  usb_setup_packet_t *setup = reinterpret_cast<usb_setup_packet_t *>(transfer->data_buffer);
  setup->bmRequestType = AUDIO_SET_CUR_REQUEST_TYPE;
  setup->bRequest = AUDIO_SET_CUR_REQUEST;
  setup->wValue = AUDIO_EP_SAMPLING_FREQ_CONTROL;
  setup->wIndex = endpointAddress;
  setup->wLength = AUDIO_SAMPLE_RATE_LENGTH;

  uint8_t *frequency = transfer->data_buffer + USB_SETUP_PACKET_SIZE;
  frequency[0] = sampleRate & 0xff;
  frequency[1] = (sampleRate >> 8) & 0xff;
  frequency[2] = (sampleRate >> 16) & 0xff;

  transfer->device_handle = device.handle;
  transfer->bEndpointAddress = 0;
  transfer->callback = controlTransferCallback;
  transfer->context = this;
  transfer->num_bytes = USB_SETUP_PACKET_SIZE + AUDIO_SAMPLE_RATE_LENGTH;

  err = usb_host_transfer_submit_control(clientHandle_, transfer);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "usb_host_transfer_submit_control(Audio SET_CUR sampling frequency ep=0x%02x) failed: %s",
             endpointAddress,
             esp_err_to_name(err));
    setLastError(err);
    usb_host_transfer_free(transfer);
    return false;
  }

  ESP_LOGD(TAG, "Audio SET_CUR sampling frequency submitted ep=0x%02x rate=%lu",
           endpointAddress,
           static_cast<unsigned long>(sampleRate));
  return true;
}

bool EspUsbHost::submitVendorSerialControl(uint8_t requestType,
                                           uint8_t request,
                                           uint16_t value,
                                           uint16_t index,
                                           const uint8_t *data,
                                           size_t length,
                                           uint8_t address)
{
  DeviceState *device = findDevice(address);
  if (!device || !device->handle)
  {
    return false;
  }

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

  transfer->device_handle = device->handle;
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
  return host_.setSerialBaudRate(baud, address_);
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
  return host_.serialReady(address_);
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
  return host_.sendSerial(buffer, size, address_) ? size : 0;
}

bool EspUsbHostCdcSerial::setBaudRate(uint32_t baud)
{
  return host_.setSerialBaudRate(baud, address_);
}

bool EspUsbHostCdcSerial::setDtr(bool enable)
{
  EspUsbHost::DeviceState *device = host_.findSerialDevice(address_);
  if (!device)
  {
    return false;
  }
  device->serialDtr = enable;
  if (device->hasCdcControlInterface)
  {
    device->cdcConfigured = false;
    host_.configureCdcAcm(*device);
  }
  else if (device->vendorSerialSupported)
  {
    host_.configureVendorSerial(*device);
  }
  return true;
}

bool EspUsbHostCdcSerial::setRts(bool enable)
{
  EspUsbHost::DeviceState *device = host_.findSerialDevice(address_);
  if (!device)
  {
    return false;
  }
  device->serialRts = enable;
  if (device->hasCdcControlInterface)
  {
    device->cdcConfigured = false;
    host_.configureCdcAcm(*device);
  }
  else if (device->vendorSerialSupported)
  {
    host_.configureVendorSerial(*device);
  }
  return true;
}

void EspUsbHostCdcSerial::setAddress(uint8_t address)
{
  address_ = address;
}

uint8_t EspUsbHostCdcSerial::address() const
{
  return address_;
}

void EspUsbHostCdcSerial::clearAddress()
{
  address_ = ESP_USB_HOST_ANY_ADDRESS;
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

bool EspUsbHostCdcSerial::accepts(uint8_t address) const
{
  return address_ == ESP_USB_HOST_ANY_ADDRESS || address_ == address;
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
