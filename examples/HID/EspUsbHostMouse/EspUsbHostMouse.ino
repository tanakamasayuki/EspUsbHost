#include "EspUsbHost.h"

EspUsbHost usb;

static int32_t posX = 0;
static int32_t posY = 0;

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

  usb.onMouse([](const EspUsbHostMouseEvent &event)
              {
    if (event.buttonsChanged)
    {
      // en: Compare current and previous button bitmasks to report edge events.
      // ja: 現在と前回のボタンビットマスクを比較し、押下/解放の変化だけを表示します。
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
      // en: Mouse reports relative deltas; accumulate them to show a simple position.
      // ja: マウスは相対移動量を報告するため、累積して簡易的な位置として表示します。
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
