#include "EspUsbHost.h"

EspUsbHost usb;

void setup()
{
  Serial.begin(115200);
  delay(500);

  Serial.println("EspUsbHost gamepad example start");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        { Serial.printf("connected: vid=%04x pid=%04x product=%s\n", info.vid, info.pid, info.product); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &)
                           { Serial.println("disconnected"); });

  usb.onGamepad([](const EspUsbHostGamepadEvent &event)
                { Serial.printf("gamepad x=%d y=%d z=%d rz=%d rx=%d ry=%d hat=%u buttons=0x%08lx previous=0x%08lx\n",
                                event.x,
                                event.y,
                                event.z,
                                event.rz,
                                event.rx,
                                event.ry,
                                event.hat,
                                static_cast<unsigned long>(event.buttons),
                                static_cast<unsigned long>(event.previousButtons)); });

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(1);
}
