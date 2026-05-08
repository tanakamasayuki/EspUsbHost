#ifndef ESP_USB_HOST_H
#define ESP_USB_HOST_H

#include <Arduino.h>
#include <functional>
#include <usb/usb_host.h>
#include <class/hid/hid.h>

#if __has_include(<rom/usb/usb_common.h>)
  #include <rom/usb/usb_common.h>
#else
  #define USB_DEVICE_DESC              0x01
  #define USB_CONFIGURATION_DESC       0x02
  #define USB_STRING_DESC              0x03
  #define USB_INTERFACE_DESC           0x04
  #define USB_ENDPOINT_DESC            0x05
  #define USB_INTERFACE_ASSOC_DESC     0x0B
  #define USB_HID_DESC                 0x21
  #define USB_HID_REPORT_DESC          0x22
#endif

enum EspUsbHostKeyboardLayout {
  ESP_USB_HOST_KEYBOARD_LAYOUT_US = 0,
  ESP_USB_HOST_KEYBOARD_LAYOUT_JP,
};

static constexpr uint8_t ESP_USB_HOST_MOUSE_LEFT = 0x01;
static constexpr uint8_t ESP_USB_HOST_MOUSE_RIGHT = 0x02;
static constexpr uint8_t ESP_USB_HOST_MOUSE_MIDDLE = 0x04;
static constexpr uint8_t ESP_USB_HOST_MOUSE_BACK = 0x08;
static constexpr uint8_t ESP_USB_HOST_MOUSE_FORWARD = 0x10;

struct EspUsbHostConfig {
  uint32_t taskStackSize = 4096;
  UBaseType_t taskPriority = 5;
  BaseType_t taskCore = tskNO_AFFINITY;
};

struct EspUsbHostDeviceInfo {
  uint8_t address = 0;
  uint16_t vid = 0;
  uint16_t pid = 0;
  const char *manufacturer = "";
  const char *product = "";
  const char *serial = "";
};

struct EspUsbHostKeyboardEvent {
  uint8_t interfaceNumber = 0;
  bool pressed = false;
  bool released = false;
  uint8_t keycode = 0;
  uint8_t ascii = 0;
  uint8_t modifiers = 0;
};

struct EspUsbHostMouseEvent {
  uint8_t interfaceNumber = 0;
  int16_t x = 0;
  int16_t y = 0;
  int16_t wheel = 0;
  uint8_t buttons = 0;
  uint8_t previousButtons = 0;
  bool moved = false;
  bool buttonsChanged = false;
};

struct EspUsbHostHIDInput {
  uint8_t address = 0;
  uint8_t interfaceNumber = 0;
  uint8_t subclass = 0;
  uint8_t protocol = 0;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

class EspUsbHost {
public:
  using DeviceCallback = std::function<void(const EspUsbHostDeviceInfo &)>;
  using KeyboardCallback = std::function<void(const EspUsbHostKeyboardEvent &)>;
  using MouseCallback = std::function<void(const EspUsbHostMouseEvent &)>;
  using HIDInputCallback = std::function<void(const EspUsbHostHIDInput &)>;

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

  void setKeyboardLayout(EspUsbHostKeyboardLayout layout);

  int lastError() const;
  const char *lastErrorName() const;

private:
  struct EndpointState {
    bool inUse = false;
    uint8_t address = 0;
    uint8_t interfaceNumber = 0;
    uint8_t interfaceClass = 0;
    uint8_t interfaceSubClass = 0;
    uint8_t interfaceProtocol = 0;
    usb_transfer_t *transfer = nullptr;
    uint8_t lastKeyboardReport[8] = {};
    bool keyboardReportReady = false;
    bool keyboardSawIdle = false;
    uint8_t keyboardIdleReports = 0;
    uint8_t lastMouseButtons = 0;
  };

  static void taskEntry(void *arg);
  static void clientEventCallback(const usb_host_client_event_msg_t *eventMsg, void *arg);
  static void transferCallback(usb_transfer_t *transfer);

  void taskLoop();
  void handleClientEvent(const usb_host_client_event_msg_t *eventMsg);
  void handleNewDevice(uint8_t address);
  void handleDeviceGone(usb_device_handle_t goneHandle);
  void parseConfigDescriptor(const usb_config_desc_t *configDesc);
  void handleDescriptor(uint8_t descriptorType, const uint8_t *data);
  void handleTransfer(usb_transfer_t *transfer);
  void handleKeyboard(EndpointState &endpoint, const uint8_t *data, size_t length);
  void handleMouse(EndpointState &endpoint, const uint8_t *data, size_t length);

  EndpointState *findEndpoint(uint8_t endpointAddress);
  EndpointState *allocateEndpoint();
  void releaseEndpoints();
  void releaseInterfaces();
  void setLastError(esp_err_t err);
  uint8_t keycodeToAscii(uint8_t keycode, uint8_t modifiers) const;
  static String usbString(const usb_str_desc_t *strDesc);

  EspUsbHostConfig config_;
  TaskHandle_t taskHandle_ = nullptr;
  volatile bool running_ = false;
  volatile bool ready_ = false;
  esp_err_t lastError_ = ESP_OK;

  usb_host_client_handle_t clientHandle_ = nullptr;
  usb_device_handle_t deviceHandle_ = nullptr;
  EspUsbHostDeviceInfo deviceInfo_;
  String manufacturer_;
  String product_;
  String serial_;

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
};

#endif
