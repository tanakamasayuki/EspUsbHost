#include "EspUsbHost.h"

class MyEspUsbHost : public EspUsbHost {
  void onMouseButtons(hid_mouse_report_t report, uint8_t last_buttons) {
    Serial.printf("last_buttons=0x%02x(%c%c%c%c%c), buttons=0x%02x(%c%c%c%c%c), x=%d, y=%d, wheel=%d\n",
                  last_buttons,
                  (last_buttons & MOUSE_BUTTON_LEFT) ? 'L' : ' ',
                  (last_buttons & MOUSE_BUTTON_RIGHT) ? 'R' : ' ',
                  (last_buttons & MOUSE_BUTTON_MIDDLE) ? 'M' : ' ',
                  (last_buttons & MOUSE_BUTTON_BACKWARD) ? 'B' : ' ',
                  (last_buttons & MOUSE_BUTTON_FORWARD) ? 'F' : ' ',
                  report.buttons,
                  (report.buttons & MOUSE_BUTTON_LEFT) ? 'L' : ' ',
                  (report.buttons & MOUSE_BUTTON_RIGHT) ? 'R' : ' ',
                  (report.buttons & MOUSE_BUTTON_MIDDLE) ? 'M' : ' ',
                  (report.buttons & MOUSE_BUTTON_BACKWARD) ? 'B' : ' ',
                  (report.buttons & MOUSE_BUTTON_FORWARD) ? 'F' : ' ',
                  report.x,
                  report.y,
                  report.wheel);

    // LEFT
    if (!(last_buttons & MOUSE_BUTTON_LEFT) && (report.buttons & MOUSE_BUTTON_LEFT)) {
      Serial.println("Mouse LEFT Click");
    }
    if ((last_buttons & MOUSE_BUTTON_LEFT) && !(report.buttons & MOUSE_BUTTON_LEFT)) {
      Serial.println("Mouse LEFT Release");
    }

    // RIGHT
    if (!(last_buttons & MOUSE_BUTTON_RIGHT) && (report.buttons & MOUSE_BUTTON_RIGHT)) {
      Serial.println("Mouse RIGHT Click");
    }
    if ((last_buttons & MOUSE_BUTTON_RIGHT) && !(report.buttons & MOUSE_BUTTON_RIGHT)) {
      Serial.println("Mouse RIGHT Release");
    }

    // MIDDLE
    if (!(last_buttons & MOUSE_BUTTON_MIDDLE) && (report.buttons & MOUSE_BUTTON_MIDDLE)) {
      Serial.println("Mouse MIDDLE Click");
    }
    if ((last_buttons & MOUSE_BUTTON_MIDDLE) && !(report.buttons & MOUSE_BUTTON_MIDDLE)) {
      Serial.println("Mouse MIDDLE Release");
    }

    // BACKWARD
    if (!(last_buttons & MOUSE_BUTTON_BACKWARD) && (report.buttons & MOUSE_BUTTON_BACKWARD)) {
      Serial.println("Mouse BACKWARD Click");
    }
    if ((last_buttons & MOUSE_BUTTON_BACKWARD) && !(report.buttons & MOUSE_BUTTON_BACKWARD)) {
      Serial.println("Mouse BACKWARD Release");
    }

    // FORWARD
    if (!(last_buttons & MOUSE_BUTTON_FORWARD) && (report.buttons & MOUSE_BUTTON_FORWARD)) {
      Serial.println("Mouse FORWARD Click");
    }
    if ((last_buttons & MOUSE_BUTTON_FORWARD) && !(report.buttons & MOUSE_BUTTON_FORWARD)) {
      Serial.println("Mouse FORWARD Release");
    }
  };

  void onMouseMove(hid_mouse_report_t report) {
    Serial.printf("buttons=0x%02x(%c%c%c%c%c), x=%d, y=%d, wheel=%d\n",
                  report.buttons,
                  (report.buttons & MOUSE_BUTTON_LEFT) ? 'L' : ' ',
                  (report.buttons & MOUSE_BUTTON_RIGHT) ? 'R' : ' ',
                  (report.buttons & MOUSE_BUTTON_MIDDLE) ? 'M' : ' ',
                  (report.buttons & MOUSE_BUTTON_BACKWARD) ? 'B' : ' ',
                  (report.buttons & MOUSE_BUTTON_FORWARD) ? 'F' : ' ',
                  report.x,
                  report.y,
                  report.wheel);
  };
};

MyEspUsbHost usbHost;

void setup() {
  Serial.begin(115200);
  delay(500);

  usbHost.begin();
}

void loop() {
  usbHost.task();
}
