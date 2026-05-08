#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_JP);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        { Serial.printf("connected: vid=%04x pid=%04x product=%s\n",
                                        device.vid,
                                        device.pid,
                                        device.product); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &)
                           { Serial.println("disconnected"); });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
      if (event.pressed && event.ascii) {
        if (event.ascii == '\r') {
          Serial.println();
        } else if (' ' <= event.ascii && event.ascii <= '~') {
          Serial.print((char)event.ascii);
        }
      } });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(1);
}
