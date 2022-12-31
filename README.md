This is a library for using USB Host with ESP32.

## Target board
- ESP32-S3-DevKitC
- M5Stack ATOMS3

## function
- USB Keybord

## Usage
```c
#include "EspUsbHostKeybord.h"

class MyEspUsbHostKeybord : public EspUsbHostKeybord {
public:
  void onKey(usb_transfer_t *transfer) {
    uint8_t *const p = transfer->data_buffer;
    Serial.printf("onKey %02x %02x %02x %02x %02x %02x %02x %02x\n", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
  };
};

MyEspUsbHostKeybord usbHost;

void setup() {
  Serial.begin(115200);
  usbHost.begin();
}

void loop() {
  usbHost.task();
}
```
