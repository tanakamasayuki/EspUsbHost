#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  Serial.begin(115200);
  delay(5000);

  usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.print("connected: ");
                          espUsbHostPrint(device); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(device); });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
    if (!event.pressed) return;

    if (event.ascii == '\r')
    {
      Serial.println();
    }
    else if (event.ascii >= 0x20 && event.ascii != 0x7F)
    {
      Serial.print((char)event.ascii);
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
