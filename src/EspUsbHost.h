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

enum EspUsbHostKeyboardLayout
{
  ESP_USB_HOST_KEYBOARD_LAYOUT_US = 0,
  ESP_USB_HOST_KEYBOARD_LAYOUT_JP,
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

struct EspUsbHostConfig
{
  uint32_t taskStackSize = 4096;
  UBaseType_t taskPriority = 5;
  BaseType_t taskCore = tskNO_AFFINITY;
};

struct EspUsbHostDeviceInfo
{
  uint8_t address = 0;
  uint16_t vid = 0;
  uint16_t pid = 0;
  const char *manufacturer = "";
  const char *product = "";
  const char *serial = "";
};

struct EspUsbHostKeyboardEvent
{
  uint8_t interfaceNumber = 0;
  bool pressed = false;
  bool released = false;
  uint8_t keycode = 0;
  uint8_t ascii = 0;
  uint8_t modifiers = 0;
};

struct EspUsbHostMouseEvent
{
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

struct EspUsbHostConsumerControlEvent
{
  uint8_t interfaceNumber = 0;
  uint16_t usage = 0;
  bool pressed = false;
  bool released = false;
};

struct EspUsbHostGamepadEvent
{
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

struct EspUsbHostVendorInput
{
  uint8_t interfaceNumber = 0;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

struct EspUsbHostSystemControlEvent
{
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
  void onConsumerControl(ConsumerControlCallback callback);
  void onGamepad(GamepadCallback callback);
  void onVendorInput(VendorInputCallback callback);
  void onSystemControl(SystemControlCallback callback);

  void setKeyboardLayout(EspUsbHostKeyboardLayout layout);
  bool sendHIDReport(uint8_t interfaceNumber,
                     uint8_t reportType,
                     uint8_t reportId,
                     const uint8_t *data,
                     size_t length);
  bool sendVendorOutput(const uint8_t *data, size_t length);
  bool sendVendorFeature(const uint8_t *data, size_t length);
  bool sendSerial(const uint8_t *data, size_t length);
  bool sendSerial(const char *text);
  bool serialReady() const;
  bool setSerialBaudRate(uint32_t baud);
  bool setKeyboardLeds(bool numLock, bool capsLock, bool scrollLock);

  int lastError() const;
  const char *lastErrorName() const;

private:
  struct EndpointState
  {
    bool inUse = false;
    uint8_t address = 0;
    uint8_t interfaceNumber = 0;
    uint8_t interfaceClass = 0;
    uint8_t interfaceSubClass = 0;
    uint8_t interfaceProtocol = 0;
    usb_transfer_t *transfer = nullptr;
    uint8_t lastKeyboardReport[8] = {};
    bool keyboardReportReady = false;
    uint8_t lastMouseButtons = 0;
    uint16_t lastConsumerUsage = 0;
    uint32_t lastGamepadButtons = 0;
    uint8_t lastSystemUsage = 0;
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
  void parseConfigDescriptor(const usb_config_desc_t *configDesc);
  void handleDescriptor(uint8_t descriptorType, const uint8_t *data);
  void handleTransfer(usb_transfer_t *transfer);
  void handleKeyboard(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleMouse(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleSerial(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleConsumerControl(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleGamepad(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleVendorInput(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleSystemControl(EndpointState &endpoint, const uint8_t *data, size_t length);

  EndpointState *findEndpoint(uint8_t endpointAddress);
  EndpointState *allocateEndpoint();
  void releaseEndpoints(bool clearEndpoints);
  void releaseInterfaces();
  void configureCdcAcm();
  void configureVendorSerial();
  bool submitVendorSerialControl(uint8_t requestType,
                                 uint8_t request,
                                 uint16_t value,
                                 uint16_t index,
                                 const uint8_t *data = nullptr,
                                 size_t length = 0);
  void attachCdcSerial(EspUsbHostCdcSerial *serial);
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
  usb_device_handle_t deviceHandle_ = nullptr;
  EspUsbHostDeviceInfo deviceInfo_;
  String manufacturer_;
  String product_;
  String serial_;
  bool hasKeyboardInterface_ = false;
  uint8_t keyboardInterfaceNumber_ = 0;
  bool hasVendorInterface_ = false;
  uint8_t vendorInterfaceNumber_ = 0;
  bool hasVendorOutEndpoint_ = false;
  uint8_t vendorOutEndpointAddress_ = 0;
  uint16_t vendorOutPacketSize_ = 0;
  bool hasCdcControlInterface_ = false;
  bool hasCdcDataInterface_ = false;
  bool cdcConfigured_ = false;
  uint8_t cdcControlInterfaceNumber_ = 0;
  uint8_t cdcDataInterfaceNumber_ = 0;
  bool hasSerialOutEndpoint_ = false;
  uint8_t serialOutEndpointAddress_ = 0;
  uint16_t serialOutPacketSize_ = 0;
  uint32_t serialBaudRate_ = 115200;
  bool serialDtr_ = true;
  bool serialRts_ = true;
  bool hasVendorSerialInterface_ = false;
  bool vendorSerialSupported_ = false;
  uint8_t vendorSerialInterfaceNumber_ = 0;
  EspUsbHostCdcSerial *cdcSerial_ = nullptr;

  EndpointState endpoints_[16];
  uint8_t interfaces_[16] = {};
  uint8_t interfaceCount_ = 0;
  uint8_t currentInterfaceNumber_ = 0;
  uint8_t currentInterfaceClass_ = 0;
  uint8_t currentInterfaceSubClass_ = 0;
  uint8_t currentInterfaceProtocol_ = 0;
  esp_err_t currentClaimResult_ = ESP_OK;

  EspUsbHostKeyboardLayout keyboardLayout_ = ESP_USB_HOST_KEYBOARD_LAYOUT_US;

  DeviceCallback deviceConnectedCallback_;
  DeviceCallback deviceDisconnectedCallback_;
  KeyboardCallback keyboardCallback_;
  MouseCallback mouseCallback_;
  HIDInputCallback hidInputCallback_;
  SerialDataCallback serialDataCallback_;
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

private:
  static constexpr size_t RX_BUFFER_SIZE = 512;

  void pushData(const uint8_t *data, size_t length);
  size_t nextIndex(size_t index) const;
  friend class EspUsbHost;

  EspUsbHost &host_;
  uint8_t rxBuffer_[RX_BUFFER_SIZE] = {};
  size_t rxHead_ = 0;
  size_t rxTail_ = 0;
  portMUX_TYPE rxMux_ = portMUX_INITIALIZER_UNLOCKED;
};

#endif
