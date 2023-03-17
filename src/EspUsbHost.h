#ifndef __EspUsbHost_H__
#define __EspUsbHost_H__

#include <Arduino.h>
#include <usb/usb_host.h>
#include <class/hid/hid.h>
#include <rom/usb/usb_common.h>

class EspUsbHost {
public:
  bool isReady = false;
  uint8_t interval;
  unsigned long lastCheck;

  struct endpoint_data_t {
    uint8_t bInterfaceNumber;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t bCountryCode;    
  };
  endpoint_data_t endpoint_data_list[17];
  uint8_t _bInterfaceNumber;
  uint8_t _bInterfaceClass;
  uint8_t _bInterfaceSubClass;
  uint8_t _bInterfaceProtocol;
  uint8_t _bCountryCode;
  esp_err_t claim_err;

  usb_host_client_handle_t clientHandle;
  usb_device_handle_t deviceHandle;
  uint32_t eventFlags;
  usb_transfer_t *usbTransfer[16];
  uint8_t usbTransferSize;
  uint8_t usbInterface[16];
  uint8_t usbInterfaceSize;

  hid_local_enum_t hidLocal;
  bool firstIteration = true;

  void begin(void);
  void task(void);

  static void _clientEventCallback(const usb_host_client_event_msg_t *eventMsg, void *arg);
  void _configCallback(const usb_config_desc_t *config_desc);
  void onConfig(const uint8_t bDescriptorType, const uint8_t *p);
  static String getUsbDescString(const usb_str_desc_t *str_desc);
  static void _onReceive(usb_transfer_t *transfer);

  static void _printPcapText(const char* title, uint16_t function, uint8_t direction, uint8_t endpoint, uint8_t type, uint8_t size, uint8_t stage, const uint8_t *data);
  esp_err_t submitControl(const uint8_t bmRequestType, const uint8_t bDescriptorIndex, const uint8_t bDescriptorType, const uint16_t wInterfaceNumber, const uint16_t wDescriptorLength);
  static void _onReceiveControl(usb_transfer_t *transfer);

  virtual void onReceive(const usb_transfer_t *transfer){};
  virtual void onGone(const usb_host_client_event_msg_t *eventMsg){this->isReady = false;};

  virtual uint8_t getKeycodeToAscii(uint8_t keycode, uint8_t shift);
  virtual void onKeyboard(hid_keyboard_report_t report, hid_keyboard_report_t last_report);
  virtual void onKeyboardKey(uint8_t ascii, uint8_t keycode, uint8_t modifier);

  virtual void onMouse(hid_mouse_report_t report, uint8_t last_buttons);
  virtual void onMouseButtons(hid_mouse_report_t report, uint8_t last_buttons);
  virtual void onMouseMove(hid_mouse_report_t report);

  void _onDataGamepad();

  void setHIDLocal(hid_local_enum_t code);

  static uint8_t getItem(uint8_t val){
    return val & 0xfc;
  }
};

#define HID_KEYCODE_TO_ASCII_JA   \
    {0     , 0      }, /* 0x00 */ \
    {0     , 0      }, /* 0x01 */ \
    {0     , 0      }, /* 0x02 */ \
    {0     , 0      }, /* 0x03 */ \
    {'a'   , 'A'    }, /* 0x04 */ \
    {'b'   , 'B'    }, /* 0x05 */ \
    {'c'   , 'C'    }, /* 0x06 */ \
    {'d'   , 'D'    }, /* 0x07 */ \
    {'e'   , 'E'    }, /* 0x08 */ \
    {'f'   , 'F'    }, /* 0x09 */ \
    {'g'   , 'G'    }, /* 0x0a */ \
    {'h'   , 'H'    }, /* 0x0b */ \
    {'i'   , 'I'    }, /* 0x0c */ \
    {'j'   , 'J'    }, /* 0x0d */ \
    {'k'   , 'K'    }, /* 0x0e */ \
    {'l'   , 'L'    }, /* 0x0f */ \
    {'m'   , 'M'    }, /* 0x10 */ \
    {'n'   , 'N'    }, /* 0x11 */ \
    {'o'   , 'O'    }, /* 0x12 */ \
    {'p'   , 'P'    }, /* 0x13 */ \
    {'q'   , 'Q'    }, /* 0x14 */ \
    {'r'   , 'R'    }, /* 0x15 */ \
    {'s'   , 'S'    }, /* 0x16 */ \
    {'t'   , 'T'    }, /* 0x17 */ \
    {'u'   , 'U'    }, /* 0x18 */ \
    {'v'   , 'V'    }, /* 0x19 */ \
    {'w'   , 'W'    }, /* 0x1a */ \
    {'x'   , 'X'    }, /* 0x1b */ \
    {'y'   , 'Y'    }, /* 0x1c */ \
    {'z'   , 'Z'    }, /* 0x1d */ \
    {'1'   , '!'    }, /* 0x1e */ \
    {'2'   , '"'    }, /* 0x1f */ \
    {'3'   , '#'    }, /* 0x20 */ \
    {'4'   , '$'    }, /* 0x21 */ \
    {'5'   , '%'    }, /* 0x22 */ \
    {'6'   , '&'    }, /* 0x23 */ \
    {'7'   , '\''   }, /* 0x24 */ \
    {'8'   , '('    }, /* 0x25 */ \
    {'9'   , ')'    }, /* 0x26 */ \
    {'0'   , 0      }, /* 0x27 */ \
    {'\r'  , '\r'   }, /* 0x28 */ \
    {'\x1b', '\x1b' }, /* 0x29 */ \
    {'\b'  , '\b'   }, /* 0x2a */ \
    {'\t'  , '\t'   }, /* 0x2b */ \
    {' '   , ' '    }, /* 0x2c */ \
    {'-'   , '='    }, /* 0x2d */ \
    {'^'   , '~'    }, /* 0x2e */ \
    {'@'   , '`'    }, /* 0x2f */ \
    {'['   , '{'    }, /* 0x30 */ \
    {0     , 0      }, /* 0x31 */ \
    {']'   , '}'    }, /* 0x32 */ \
    {';'   , '+'    }, /* 0x33 */ \
    {':'   , '*'    }, /* 0x34 */ \
    {0     , 0      }, /* 0x35 HANKAKU */ \
    {','   , '<'    }, /* 0x36 */ \
    {'.'   , '>'    }, /* 0x37 */ \
    {'/'   , '?'    }, /* 0x38 */ \
                                  \
    {0     , 0      }, /* 0x39 */ \
    {0     , 0      }, /* 0x3a */ \
    {0     , 0      }, /* 0x3b */ \
    {0     , 0      }, /* 0x3c */ \
    {0     , 0      }, /* 0x3d */ \
    {0     , 0      }, /* 0x3e */ \
    {0     , 0      }, /* 0x3f */ \
    {0     , 0      }, /* 0x40 */ \
    {0     , 0      }, /* 0x41 */ \
    {0     , 0      }, /* 0x42 */ \
    {0     , 0      }, /* 0x43 */ \
    {0     , 0      }, /* 0x44 */ \
    {0     , 0      }, /* 0x45 */ \
    {0     , 0      }, /* 0x46 */ \
    {0     , 0      }, /* 0x47 */ \
    {0     , 0      }, /* 0x48 */ \
    {0     , 0      }, /* 0x49 */ \
    {0     , 0      }, /* 0x4a */ \
    {0     , 0      }, /* 0x4b */ \
    {0     , 0      }, /* 0x4c */ \
    {0     , 0      }, /* 0x4d */ \
    {0     , 0      }, /* 0x4e */ \
    {0     , 0      }, /* 0x4f */ \
    {0     , 0      }, /* 0x50 */ \
    {0     , 0      }, /* 0x51 */ \
    {0     , 0      }, /* 0x52 */ \
    {0     , 0      }, /* 0x53 */ \
                                  \
    {'/'   , '/'    }, /* 0x54 */ \
    {'*'   , '*'    }, /* 0x55 */ \
    {'-'   , '-'    }, /* 0x56 */ \
    {'+'   , '+'    }, /* 0x57 */ \
    {'\r'  , '\r'   }, /* 0x58 */ \
    {'1'   , 0      }, /* 0x59 */ \
    {'2'   , 0      }, /* 0x5a */ \
    {'3'   , 0      }, /* 0x5b */ \
    {'4'   , 0      }, /* 0x5c */ \
    {'5'   , '5'    }, /* 0x5d */ \
    {'6'   , 0      }, /* 0x5e */ \
    {'7'   , 0      }, /* 0x5f */ \
    {'8'   , 0      }, /* 0x60 */ \
    {'9'   , 0      }, /* 0x61 */ \
    {'0'   , 0      }, /* 0x62 */ \
    {'.'   , 0      }, /* 0x63 */ \
    {0     , 0      }, /* 0x64 */ \
    {0     , 0      }, /* 0x65 */ \
    {0     , 0      }, /* 0x66 */ \
    {'='   , '='    }, /* 0x67 */ \

#endif
