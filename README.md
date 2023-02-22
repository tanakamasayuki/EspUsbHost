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
  void onKeyboardType(uint8_t ascii, uint8_t keycode, uint8_t modifier) {
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
