#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println("EspUsbHost consumer control example start");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.print("connected: ");
                          espUsbHostPrint(device); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(device); });

  usb.onConsumerControl([](const EspUsbHostConsumerControlEvent &event)
                        { Serial.printf("consumer %s usage=0x%04x %s\n",
                                        event.pressed ? "press" : "release",
                                        event.usage,
                                        espUsbHostConsumerControlUsageName(event.usage)); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(1);
}
