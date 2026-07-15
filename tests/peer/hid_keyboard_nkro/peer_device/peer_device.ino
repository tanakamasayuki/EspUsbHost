#include "EspUsbDevice.h"

// Device side of the NKRO peer test: an NKRO keyboard that, on command, holds an
// 8-key chord all at once (impossible with the 6-key boot report) and then
// releases it.

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

// Eight keys held simultaneously — two more than the 6KRO boot limit.
static const uint8_t chord[] = {
    ESP_USB_HID_KEY_A, ESP_USB_HID_KEY_S, ESP_USB_HID_KEY_D, ESP_USB_HID_KEY_F,
    ESP_USB_HID_KEY_G, ESP_USB_HID_KEY_H, ESP_USB_HID_KEY_J, ESP_USB_HID_KEY_K,
};

void setup()
{
  Serial.begin(115200);
  delay(300);

  keyboard.enableNkro();
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);
  keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &report)
                          {
                            Serial.printf("LED numlock=%u capslock=%u scrolllock=%u\n",
                                          report.numLock ? 1 : 0,
                                          report.capsLock ? 1 : 0,
                                          report.scrollLock ? 1 : 0);
                          });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4033;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice NKRO Keyboard";
  config.serialNumber = "espusb-keyboard-nkro";
  device.begin(config);
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.printf("DEVICE_READY nkro=%u ready=%u\n",
                    keyboard.nkroEnabled() ? 1 : 0,
                    device.ready() ? 1 : 0);
    }
    else if (command == 'c')
    {
      // pressUsage() accumulates key bits and sends the full bitmap each call,
      // but ignores holdMs, so calling it back-to-back lets the HID IN endpoint
      // overwrite a report before the host has polled it (only the first key
      // survives). Space the presses out so every incremental report — and the
      // final 8-key report — is actually delivered, then hold and release.
      for (size_t i = 0; i < sizeof(chord); i++)
      {
        keyboard.pressUsage(chord[i]);
        delay(25);
      }
      delay(150);
      keyboard.releaseAll();
      Serial.printf("SENT_CHORD n=%u protocol=%s\n",
                    (unsigned)sizeof(chord),
                    keyboard.protocol() == 0 ? "boot" : "report");
    }
  }
  delay(1);
}
