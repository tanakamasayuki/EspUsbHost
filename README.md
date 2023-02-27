This is a library for using USB Host with ESP32.

## Target board
- ESP32-S3-DevKitC
- M5Stack ATOMS3

## function
- USB Keybord
- USB Mouse

## Usage
```c
#include "EspUsbHost.h"

class MyEspUsbHost : public EspUsbHost {
  void onKeyboardKey(uint8_t ascii, uint8_t keycode, uint8_t modifier) {
    if (' ' <= ascii && ascii <= '~') {
      Serial.printf("%c", ascii);
    } else if (ascii == '\r') {
      Serial.println();
    }
  };
};

MyEspUsbHost usbHost;

void setup() {
  Serial.begin(115200);
  delay(500);

  usbHost.begin();
  usbHost.setHIDLocal(HID_LOCAL_Japan_Katakana);
}

void loop() {
  usbHost.task();
}
```

## Virtual function

### common

- virtual void onData(const usb_transfer_t *transfer);
- virtual void onGone(const usb_host_client_event_msg_t *eventMsg);

### Keyboard

- virtual uint8_t getKeycodeToAscii(uint8_t keycode, uint8_t shift);
- virtual void onKeyboard(hid_keyboard_report_t report, hid_keyboard_report_t last_report);
- virtual void onKeyboardKey(uint8_t ascii, uint8_t keycode, uint8_t modifier);

### Mouse

- virtual void onMouse(hid_mouse_report_t report, uint8_t last_buttons);
- virtual void onMouseButtons(hid_mouse_report_t report, uint8_t last_buttons);
- virtual void onMouseMove(hid_mouse_report_t report);
