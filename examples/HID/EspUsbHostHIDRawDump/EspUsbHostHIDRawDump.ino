#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.print("connected: ");
                          espUsbHostPrint(device); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(device); });

  usb.onHIDInput([](const EspUsbHostHIDInput &input)
                 {
                   // en: Raw HID input fires before any keyboard/mouse/vendor-specific parsing.
                   // ja: Raw HID入力は、キーボード/マウス/ベンダー固有の解析前に通知されます。
                   espUsbHostPrint(input);
                 });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
}
