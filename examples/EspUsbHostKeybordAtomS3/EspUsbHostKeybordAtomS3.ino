#include <M5Unified.h>
#include "EspUsbHostKeybord.h"

class MyEspUsbHostKeybord : public EspUsbHostKeybord {
public:
  void onKey(usb_transfer_t *transfer) {
    uint8_t *const p = transfer->data_buffer;
    M5.Display.clear();
    M5.Display.setCursor(0, 0);
    M5.Display.printf("%02x\n%02x\n%02x\n%02x\n%02x\n%02x\n%02x\n%02x", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
  };
};

MyEspUsbHostKeybord usbHost;

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setTextSize(2);
  M5.Display.println("EspUsbHostKeybord");

  usbHost.begin();
}

void loop() {
  usbHost.task();
}
