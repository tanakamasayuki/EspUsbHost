#include "EspUsbHost.h"

EspUsbHost usb;

static int32_t posX = 0;
static int32_t posY = 0;

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        { Serial.printf("connected: vid=%04x pid=%04x product=%s\n",
                                        device.vid, device.pid, device.product); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &)
                           { Serial.println("disconnected"); });

  usb.onMouse([](const EspUsbHostMouseEvent &event)
              {
    if (event.buttonsChanged)
    {
      uint8_t pressed  = event.buttons & ~event.previousButtons;
      uint8_t released = event.previousButtons & ~event.buttons;
      if (pressed  & 0x01) Serial.println("left   press");
      if (released & 0x01) Serial.println("left   release");
      if (pressed  & 0x02) Serial.println("right  press");
      if (released & 0x02) Serial.println("right  release");
      if (pressed  & 0x04) Serial.println("middle press");
      if (released & 0x04) Serial.println("middle release");
    }
    if (event.moved)
    {
      posX += event.x;
      posY += event.y;
      Serial.printf("pos: x=%ld y=%ld  delta: dx=%d dy=%d wheel=%d\n",
                    (long)posX, (long)posY,
                    (int)event.x, (int)event.y, (int)event.wheel);
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
