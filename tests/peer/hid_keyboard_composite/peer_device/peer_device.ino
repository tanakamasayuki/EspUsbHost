#include "EspUsbDevice.h"

// Device side of the composite-HID LED peer test: keyboard + consumer control +
// mouse merged into one HID interface with report IDs. The interface carries no
// boot subclass/protocol, so the host can only find the keyboard (and its LED
// output report) through the HID report descriptor.

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidConsumerControl consumer(device);
EspUsbDeviceHidMouse mouse(device);

void setup()
{
  Serial.begin(115200);
  delay(300);

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
  config.pid = 0x4034;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Composite HID";
  config.serialNumber = "espusb-composite-hid";
  device.begin(config);
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.printf("DEVICE_READY ready=%u\n", device.ready() ? 1 : 0);
    }
    else if (command == 'k')
    {
      keyboard.tapKey('k');
      Serial.println("SENT_KEY");
    }
    else if (command == 'v')
    {
      consumer.click(ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP);
      Serial.println("SENT_CONSUMER");
    }
    else if (command == 'm')
    {
      mouse.move(40, 0);
      Serial.println("SENT_MOUSE");
    }
  }
  delay(1);
}
