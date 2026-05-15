#ifndef ESP_USB_HOST_H
#define ESP_USB_HOST_H

#include <Arduino.h>
#include <functional>
#include <usb/usb_host.h>
#include <class/hid/hid.h>

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

enum EspUsbHostKeyboardLayout : uint16_t
{
  ESP_USB_HOST_KEYBOARD_LAYOUT_ZH_TW = 0x0404,
  ESP_USB_HOST_KEYBOARD_LAYOUT_DA_DK = 0x0406,
  ESP_USB_HOST_KEYBOARD_LAYOUT_DE_DE = 0x0407,
  ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US = 0x0409,
  ESP_USB_HOST_KEYBOARD_LAYOUT_FI_FI = 0x040B,
  ESP_USB_HOST_KEYBOARD_LAYOUT_FR_FR = 0x040C,
  ESP_USB_HOST_KEYBOARD_LAYOUT_HU_HU = 0x040E,
  ESP_USB_HOST_KEYBOARD_LAYOUT_IT_IT = 0x0410,
  ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP = 0x0411,
  ESP_USB_HOST_KEYBOARD_LAYOUT_KO_KR = 0x0412,
  ESP_USB_HOST_KEYBOARD_LAYOUT_NL_NL = 0x0413,
  ESP_USB_HOST_KEYBOARD_LAYOUT_NB_NO = 0x0414,
  ESP_USB_HOST_KEYBOARD_LAYOUT_PT_BR = 0x0416,
  ESP_USB_HOST_KEYBOARD_LAYOUT_SV_SE = 0x041D,
  ESP_USB_HOST_KEYBOARD_LAYOUT_ZH_CN = 0x0804,
  ESP_USB_HOST_KEYBOARD_LAYOUT_EN_GB = 0x0809,
  ESP_USB_HOST_KEYBOARD_LAYOUT_PT_PT = 0x0816,
  ESP_USB_HOST_KEYBOARD_LAYOUT_ES_ES = 0x0C0A,
  ESP_USB_HOST_KEYBOARD_LAYOUT_FR_CH = 0x100C,
};

enum EspUsbHostPort
{
  ESP_USB_HOST_PORT_DEFAULT = 0,
  ESP_USB_HOST_PORT_HIGH_SPEED,
  ESP_USB_HOST_PORT_FULL_SPEED,
};

static constexpr uint8_t ESP_USB_HOST_MOUSE_LEFT = 0x01;
static constexpr uint8_t ESP_USB_HOST_MOUSE_RIGHT = 0x02;
static constexpr uint8_t ESP_USB_HOST_MOUSE_MIDDLE = 0x04;
static constexpr uint8_t ESP_USB_HOST_MOUSE_BACK = 0x08;
static constexpr uint8_t ESP_USB_HOST_MOUSE_FORWARD = 0x10;

static constexpr uint8_t ESP_USB_HOST_HID_REPORT_TYPE_INPUT = 0x01;
static constexpr uint8_t ESP_USB_HOST_HID_REPORT_TYPE_OUTPUT = 0x02;
static constexpr uint8_t ESP_USB_HOST_HID_REPORT_TYPE_FEATURE = 0x03;

static constexpr uint8_t ESP_USB_HOST_KEYBOARD_LED_NUM_LOCK = 0x01;
static constexpr uint8_t ESP_USB_HOST_KEYBOARD_LED_CAPS_LOCK = 0x02;
static constexpr uint8_t ESP_USB_HOST_KEYBOARD_LED_SCROLL_LOCK = 0x04;
static constexpr uint8_t ESP_USB_HOST_KEYBOARD_LED_COMPOSE = 0x08;
static constexpr uint8_t ESP_USB_HOST_KEYBOARD_LED_KANA = 0x10;

static constexpr uint8_t ESP_USB_HOST_HID_REPORT_ID_KEYBOARD = 0x01;
static constexpr uint8_t ESP_USB_HOST_HID_REPORT_ID_MOUSE = 0x02;
static constexpr uint8_t ESP_USB_HOST_HID_REPORT_ID_GAMEPAD = 0x03;
static constexpr uint8_t ESP_USB_HOST_HID_REPORT_ID_CONSUMER_CONTROL = 0x04;
static constexpr uint8_t ESP_USB_HOST_HID_REPORT_ID_SYSTEM_CONTROL = 0x05;
static constexpr uint8_t ESP_USB_HOST_HID_REPORT_ID_VENDOR = 0x06;

static constexpr uint8_t ESP_USB_HOST_SYSTEM_CONTROL_POWER_OFF = 0x01;
static constexpr uint8_t ESP_USB_HOST_SYSTEM_CONTROL_STANDBY = 0x02;
static constexpr uint8_t ESP_USB_HOST_SYSTEM_CONTROL_WAKE_HOST = 0x03;
static constexpr uint8_t ESP_USB_HOST_ANY_ADDRESS = 0xff;
static constexpr size_t ESP_USB_HOST_MAX_DEVICES = 8;
static constexpr size_t ESP_USB_HOST_MAX_INTERFACES = 16;
static constexpr size_t ESP_USB_HOST_MAX_ENDPOINTS = 16;
static constexpr size_t ESP_USB_HOST_MAX_AUDIO_STREAMS = 8;
static constexpr size_t ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES = 4;
static constexpr size_t ESP_USB_HOST_MAX_CDC_SERIALS = 4;

struct EspUsbHostConfig
{
  uint32_t taskStackSize = 4096;
  UBaseType_t taskPriority = 5;
  BaseType_t taskCore = tskNO_AFFINITY;
  EspUsbHostPort port = ESP_USB_HOST_PORT_DEFAULT;
};

struct EspUsbHostDeviceInfo
{
  uint8_t address = 0;
  uint16_t vid = 0;
  uint16_t pid = 0;
  const char *manufacturer = "";
  const char *product = "";
  const char *serial = "";
  uint8_t parentAddress = 0;
  uint8_t portId = 0;
  usb_speed_t speed = USB_SPEED_FULL;
  uint16_t usbVersion = 0;
  uint16_t deviceVersion = 0;
  uint8_t deviceClass = 0;
  uint8_t deviceSubClass = 0;
  uint8_t deviceProtocol = 0;
  uint8_t maxPacketSize0 = 0;
  uint8_t configurationValue = 0;
  uint8_t configurationAttributes = 0;
  uint8_t configurationMaxPower = 0;
  uint8_t configurationInterfaceCount = 0;
  uint16_t configurationTotalLength = 0;
};

struct EspUsbHostInterfaceInfo
{
  uint8_t number = 0;
  uint8_t alternate = 0;
  uint8_t interfaceClass = 0;
  uint8_t interfaceSubClass = 0;
  uint8_t interfaceProtocol = 0;
  uint8_t endpointCount = 0;
};

struct EspUsbHostEndpointInfo
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t attributes = 0;
  uint16_t maxPacketSize = 0;
  uint8_t interval = 0;
};

struct EspUsbHostKeyboardEvent
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  bool pressed = false;
  bool released = false;
  uint8_t keycode = 0;
  uint8_t ascii = 0;
  uint8_t modifiers = 0;
  bool numLock = false;
  bool capsLock = false;
  bool scrollLock = false;
};

struct EspUsbHostMouseEvent
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  int16_t x = 0;
  int16_t y = 0;
  int16_t wheel = 0;
  uint8_t buttons = 0;
  uint8_t previousButtons = 0;
  bool moved = false;
  bool buttonsChanged = false;
};

struct EspUsbHostHIDInput
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t subclass = 0;
  uint8_t protocol = 0;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

struct EspUsbHostSerialData
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

struct EspUsbHostMidiMessage
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t cable = 0;
  uint8_t codeIndex = 0;
  uint8_t status = 0;
  uint8_t data1 = 0;
  uint8_t data2 = 0;
  const uint8_t *raw = nullptr;
  size_t length = 0;
};

struct EspUsbHostAudioData
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

struct EspUsbHostAudioStreamInfo
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t alternate = 0;
  uint8_t endpointAddress = 0;
  bool input = false;
  bool output = false;
  uint8_t channels = 0;
  uint8_t bytesPerSample = 0;
  uint8_t bitsPerSample = 0;
  uint32_t sampleRate = 0;
  uint8_t sampleRateCount = 0;
  uint32_t sampleRates[ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES] = {};
  uint32_t sampleRateMin = 0;
  uint32_t sampleRateMax = 0;
  uint32_t sampleRateResolution = 0;
  uint16_t maxPacketSize = 0;
  uint8_t interval = 0;
};

inline bool espUsbHostAudioStreamSupportsSampleRate(const EspUsbHostAudioStreamInfo &stream, uint32_t sampleRate)
{
  if (sampleRate == 0)
  {
    return false;
  }

  if (stream.sampleRateCount > 0)
  {
    for (uint8_t i = 0; i < stream.sampleRateCount && i < ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES; i++)
    {
      if (stream.sampleRates[i] == sampleRate)
      {
        return true;
      }
    }
    return false;
  }

  if (stream.sampleRateMin > 0 && stream.sampleRateMax >= stream.sampleRateMin)
  {
    if (sampleRate < stream.sampleRateMin || sampleRate > stream.sampleRateMax)
    {
      return false;
    }
    if (stream.sampleRateResolution == 0)
    {
      return true;
    }
    return ((sampleRate - stream.sampleRateMin) % stream.sampleRateResolution) == 0;
  }

  return stream.sampleRate == 0 || stream.sampleRate == sampleRate;
}

inline bool espUsbHostAudioStreamMatchesPcm(const EspUsbHostAudioStreamInfo &stream,
                                            uint8_t channels,
                                            uint8_t bytesPerSample,
                                            uint8_t bitsPerSample,
                                            uint32_t sampleRate)
{
  return stream.channels == channels &&
         stream.bytesPerSample == bytesPerSample &&
         stream.bitsPerSample == bitsPerSample &&
         espUsbHostAudioStreamSupportsSampleRate(stream, sampleRate);
}

struct EspUsbHostConsumerControlEvent
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint16_t usage = 0;
  bool pressed = false;
  bool released = false;
};

struct EspUsbHostGamepadEvent
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  int8_t x = 0;
  int8_t y = 0;
  int8_t z = 0;
  int8_t rz = 0;
  int8_t rx = 0;
  int8_t ry = 0;
  uint8_t hat = 0;
  uint32_t buttons = 0;
  uint32_t previousButtons = 0;
  bool changed = false;
};

struct EspUsbHostGamepadPrevState
{
  int8_t x = 0, y = 0, z = 0, rz = 0, rx = 0, ry = 0;
  uint8_t hat = 0;
  uint32_t buttons = 0;
};

struct EspUsbHostVendorInput
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

struct EspUsbHostSystemControlEvent
{
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t usage = 0;
  bool pressed = false;
  bool released = false;
};

class EspUsbHostCdcSerial;

class EspUsbHost
{
public:
  using DeviceCallback = std::function<void(const EspUsbHostDeviceInfo &)>;
  using KeyboardCallback = std::function<void(const EspUsbHostKeyboardEvent &)>;
  using MouseCallback = std::function<void(const EspUsbHostMouseEvent &)>;
  using HIDInputCallback = std::function<void(const EspUsbHostHIDInput &)>;
  using SerialDataCallback = std::function<void(const EspUsbHostSerialData &)>;
  using MidiMessageCallback = std::function<void(const EspUsbHostMidiMessage &)>;
  using AudioDataCallback = std::function<void(const EspUsbHostAudioData &)>;
  using ConsumerControlCallback = std::function<void(const EspUsbHostConsumerControlEvent &)>;
  using GamepadCallback = std::function<void(const EspUsbHostGamepadEvent &)>;
  using VendorInputCallback = std::function<void(const EspUsbHostVendorInput &)>;
  using SystemControlCallback = std::function<void(const EspUsbHostSystemControlEvent &)>;

  EspUsbHost();
  ~EspUsbHost();

  bool begin();
  bool begin(const EspUsbHostConfig &config);
  void end();
  bool ready() const;

  void onDeviceConnected(DeviceCallback callback);
  void onDeviceDisconnected(DeviceCallback callback);
  void onKeyboard(KeyboardCallback callback);
  void onMouse(MouseCallback callback);
  void onHIDInput(HIDInputCallback callback);
  void onSerialData(SerialDataCallback callback);
  void onMidiMessage(MidiMessageCallback callback);
  void onAudioData(AudioDataCallback callback);
  void onConsumerControl(ConsumerControlCallback callback);
  void onGamepad(GamepadCallback callback);
  void onVendorInput(VendorInputCallback callback);
  void onSystemControl(SystemControlCallback callback);

  void setKeyboardLayout(EspUsbHostKeyboardLayout layout);
  bool sendSetProtocol(uint8_t interfaceNumber, uint8_t address);
  bool sendHIDReport(uint8_t interfaceNumber,
                     uint8_t reportType,
                     uint8_t reportId,
                     const uint8_t *data,
                     size_t length,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool sendVendorOutput(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool sendVendorFeature(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool sendSerial(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool sendSerial(const char *text, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool serialReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool setSerialBaudRate(uint32_t baud, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool audioReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool audioOutputReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool setAudioSampleRate(uint32_t sampleRate, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool audioSend(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSend(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendControlChange(uint8_t channel, uint8_t control, uint8_t value, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendProgramChange(uint8_t channel, uint8_t program, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendPolyPressure(uint8_t channel, uint8_t note, uint8_t pressure, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendChannelPressure(uint8_t channel, uint8_t pressure, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendPitchBend(uint8_t channel, uint16_t value, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendPitchBendSigned(uint8_t channel, int16_t value, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool midiSendSysEx(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool setKeyboardLeds(bool numLock, bool capsLock, bool scrollLock, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  bool getKeyboardNumLock(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool getKeyboardCapsLock(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  bool getKeyboardScrollLock(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
  size_t deviceCount() const;
  size_t getDevices(EspUsbHostDeviceInfo *devices, size_t maxDevices) const;
  bool getDevice(uint8_t address, EspUsbHostDeviceInfo &device) const;
  size_t getInterfaces(uint8_t address, EspUsbHostInterfaceInfo *interfaces, size_t maxInterfaces) const;
  size_t getEndpoints(uint8_t address, EspUsbHostEndpointInfo *endpoints, size_t maxEndpoints) const;
  size_t getAudioStreams(uint8_t address, EspUsbHostAudioStreamInfo *streams, size_t maxStreams) const;

  int lastError() const;
  const char *lastErrorName() const;

private:
  struct EndpointState
  {
    bool inUse = false;
    uint8_t deviceIndex = 0xff;
    uint8_t deviceAddress = 0;
    usb_device_handle_t deviceHandle = nullptr;
    uint8_t address = 0;
    uint8_t interfaceNumber = 0;
    uint8_t interfaceClass = 0;
    uint8_t interfaceSubClass = 0;
    uint8_t interfaceProtocol = 0;
    usb_transfer_t *transfer = nullptr;
    bool transferSubmitted = false;
    bool resubmitAfterLed = false;
    uint8_t lastKeyboardReport[8] = {};
    bool keyboardReportReady = false;
    uint8_t lastMouseButtons = 0;
    uint16_t lastConsumerUsage = 0;
    EspUsbHostGamepadPrevState lastGamepadState;
    uint8_t lastSystemUsage = 0;
  };

  struct DeviceState
  {
    bool inUse = false;
    usb_device_handle_t handle = nullptr;
    EspUsbHostDeviceInfo info;
    String manufacturer;
    String product;
    String serial;
    bool hasKeyboardInterface = false;
    uint8_t keyboardInterfaceNumber = 0;
    bool keyboardNumLock = true;
    bool keyboardCapsLock = false;
    bool keyboardScrollLock = false;
    bool keyboardLedPending = false;
    bool keyboardLedDirty = false;
    uint32_t keyboardLedDirtyTimeMs = 0;
    uint8_t keyboardLedLastSent = 0;
    bool hasVendorInterface = false;
    uint8_t vendorInterfaceNumber = 0;
    bool hasVendorOutEndpoint = false;
    uint8_t vendorOutEndpointAddress = 0;
    uint16_t vendorOutPacketSize = 0;
    bool hasCdcControlInterface = false;
    bool hasCdcDataInterface = false;
    bool cdcConfigured = false;
    uint8_t cdcControlInterfaceNumber = 0;
    uint8_t cdcDataInterfaceNumber = 0;
    bool hasSerialOutEndpoint = false;
    uint8_t serialOutEndpointAddress = 0;
    uint16_t serialOutPacketSize = 0;
    uint32_t serialBaudRate = 115200;
    bool serialDtr = true;
    bool serialRts = true;
    bool hasVendorSerialInterface = false;
    bool vendorSerialSupported = false;
    uint8_t vendorSerialInterfaceNumber = 0;
    bool hasMidiInterface = false;
    uint8_t midiInterfaceNumber = 0;
    bool hasMidiOutEndpoint = false;
    uint8_t midiOutEndpointAddress = 0;
    uint16_t midiOutPacketSize = 0;
    bool hasAudioInterface = false;
    uint8_t audioInterfaceNumber = 0;
    bool hasAudioOutEndpoint = false;
    uint8_t audioOutInterfaceNumber = 0;
    uint8_t audioOutEndpointAddress = 0;
    uint16_t audioOutPacketSize = 0;
    uint32_t audioSampleRate = 48000;
    EspUsbHostAudioStreamInfo audioStreamInfos[ESP_USB_HOST_MAX_AUDIO_STREAMS] = {};
    uint8_t audioStreamInfoCount = 0;
    EspUsbHostInterfaceInfo interfaceInfos[ESP_USB_HOST_MAX_INTERFACES] = {};
    uint8_t interfaceInfoCount = 0;
    EspUsbHostEndpointInfo endpointInfos[ESP_USB_HOST_MAX_ENDPOINTS] = {};
    uint8_t endpointInfoCount = 0;
    uint8_t interfaces[ESP_USB_HOST_MAX_INTERFACES] = {};
    uint8_t interfaceCount = 0;
    bool isHub = false;
    uint8_t hubIndex = 0;
  };

  static void taskEntry(void *arg);
  static void clientTaskEntry(void *arg);
  static void clientEventCallback(const usb_host_client_event_msg_t *eventMsg, void *arg);
  static void transferCallback(usb_transfer_t *transfer);
  static void controlTransferCallback(usb_transfer_t *transfer);
  static void outputTransferCallback(usb_transfer_t *transfer);
  static void serialOutTransferCallback(usb_transfer_t *transfer);

  void taskLoop();
  void clientTaskLoop();
  void handleClientEvent(const usb_host_client_event_msg_t *eventMsg);
  void handleNewDevice(uint8_t address);
  void handleDeviceGone(usb_device_handle_t goneHandle);
  void parseConfigDescriptor(DeviceState &device, const usb_config_desc_t *configDesc);
  void handleDescriptor(uint8_t descriptorType, const uint8_t *data);
  void recordAudioStream(DeviceState &device, const usb_ep_desc_t *ep, bool input);
  void handleTransfer(usb_transfer_t *transfer);
  void handleKeyboard(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleMouse(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleSerial(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleMidi(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleAudio(EndpointState &endpoint, usb_transfer_t *transfer);
  void handleConsumerControl(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleGamepad(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleVendorInput(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleSystemControl(EndpointState &endpoint, const uint8_t *data, size_t length);

  EndpointState *findEndpoint(usb_device_handle_t deviceHandle, uint8_t endpointAddress);
  EndpointState *allocateEndpoint(DeviceState &device);
  DeviceState *allocateDevice();
  DeviceState *findDevice(uint8_t address);
  const DeviceState *findDevice(uint8_t address) const;
  DeviceState *findDeviceByHandle(usb_device_handle_t handle);
  DeviceState *findSerialDevice(uint8_t address);
  const DeviceState *findSerialDevice(uint8_t address) const;
  DeviceState *findMidiDevice(uint8_t address);
  const DeviceState *findMidiDevice(uint8_t address) const;
  DeviceState *findAudioOutputDevice(uint8_t address);
  const DeviceState *findAudioOutputDevice(uint8_t address) const;
  const DeviceState *findAudioDevice(uint8_t address) const;
  DeviceState *findKeyboardDevice(uint8_t address);
  const DeviceState *findKeyboardDevice(uint8_t address) const;
  DeviceState *findVendorDevice(uint8_t address);
  void releaseEndpoints(DeviceState &device, bool clearEndpoints);
  void releaseAllEndpoints(bool clearEndpoints);
  void releaseInterfaces(DeviceState &device);
  void configureCdcAcm(DeviceState &device);
  void configureVendorSerial(DeviceState &device);
  bool submitInputTransfer(EndpointState &endpoint);
  void submitPendingTransfers(usb_device_handle_t deviceHandle, uint8_t interfaceNumber);
  bool submitSetInterface(DeviceState &device, uint8_t interfaceNumber, uint8_t alternateSetting);
  bool submitAudioSamplingFrequency(DeviceState &device, uint8_t endpointAddress, uint32_t sampleRate);
  bool submitAudioOutputTransfer(DeviceState &device, const uint8_t *data, size_t length);
  bool submitVendorSerialControl(uint8_t requestType,
                                 uint8_t request,
                                 uint16_t value,
                                 uint16_t index,
                                 const uint8_t *data = nullptr,
                                 size_t length = 0,
                                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
  void attachCdcSerial(EspUsbHostCdcSerial *serial);
  void detachCdcSerial(EspUsbHostCdcSerial *serial);
  void setLastError(esp_err_t err);
  static String usbString(const usb_str_desc_t *strDesc);
  friend class EspUsbHostCdcSerial;

  EspUsbHostConfig config_;
  TaskHandle_t taskHandle_ = nullptr;
  TaskHandle_t clientTaskHandle_ = nullptr;
  volatile bool running_ = false;
  volatile bool ready_ = false;
  esp_err_t lastError_ = ESP_OK;

  usb_host_client_handle_t clientHandle_ = nullptr;
  bool sendKeyboardLedReport(DeviceState &device, uint8_t leds);
  DeviceState devices_[ESP_USB_HOST_MAX_DEVICES];
  DeviceState *currentDevice_ = nullptr;
  EspUsbHostCdcSerial *cdcSerials_[ESP_USB_HOST_MAX_CDC_SERIALS] = {};
  uint32_t defaultSerialBaudRate_ = 115200;
  uint32_t defaultAudioSampleRate_ = 48000;
  uint8_t nextHubIndex_ = 1;

  EndpointState endpoints_[16];
  uint8_t currentInterfaceNumber_ = 0;
  uint8_t currentInterfaceAlternate_ = 0;
  uint8_t currentInterfaceClass_ = 0;
  uint8_t currentInterfaceSubClass_ = 0;
  uint8_t currentInterfaceProtocol_ = 0;
  uint8_t currentAudioChannels_ = 0;
  uint8_t currentAudioBytesPerSample_ = 0;
  uint8_t currentAudioBitsPerSample_ = 0;
  uint32_t currentAudioSampleRate_ = 0;
  uint8_t currentAudioSampleRateCount_ = 0;
  uint32_t currentAudioSampleRates_[ESP_USB_HOST_MAX_AUDIO_SAMPLE_RATES] = {};
  uint32_t currentAudioSampleRateMin_ = 0;
  uint32_t currentAudioSampleRateMax_ = 0;
  uint32_t currentAudioSampleRateResolution_ = 0;
  esp_err_t currentClaimResult_ = ESP_OK;

  EspUsbHostKeyboardLayout keyboardLayout_ = ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US;

  DeviceCallback deviceConnectedCallback_;
  DeviceCallback deviceDisconnectedCallback_;
  KeyboardCallback keyboardCallback_;
  MouseCallback mouseCallback_;
  HIDInputCallback hidInputCallback_;
  SerialDataCallback serialDataCallback_;
  MidiMessageCallback midiMessageCallback_;
  AudioDataCallback audioDataCallback_;
  ConsumerControlCallback consumerControlCallback_;
  GamepadCallback gamepadCallback_;
  VendorInputCallback vendorInputCallback_;
  SystemControlCallback systemControlCallback_;
};

class EspUsbHostCdcSerial : public Stream
{
public:
  explicit EspUsbHostCdcSerial(EspUsbHost &host);

  bool begin(uint32_t baud = 115200);
  void end();
  bool connected() const;

  int available() override;
  int read() override;
  int peek() override;
  void flush() override;
  size_t write(uint8_t data) override;
  size_t write(const uint8_t *buffer, size_t size) override;
  using Print::write;

  bool setBaudRate(uint32_t baud);
  bool setDtr(bool enable);
  bool setRts(bool enable);
  void setAddress(uint8_t address);
  uint8_t address() const;
  void clearAddress();

private:
  static constexpr size_t RX_BUFFER_SIZE = 512;

  void pushData(const uint8_t *data, size_t length);
  bool accepts(uint8_t address) const;
  size_t nextIndex(size_t index) const;
  friend class EspUsbHost;

  EspUsbHost &host_;
  uint8_t address_ = ESP_USB_HOST_ANY_ADDRESS;
  uint8_t rxBuffer_[RX_BUFFER_SIZE] = {};
  size_t rxHead_ = 0;
  size_t rxTail_ = 0;
  portMUX_TYPE rxMux_ = portMUX_INITIALIZER_UNLOCKED;
};

#endif
