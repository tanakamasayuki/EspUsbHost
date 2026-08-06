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
      //     buttonMask carries every button the mouse declares (a gaming mouse
      //     often has more than the three the boot report can express).
      // ja: 現在と前回のボタンビットマスクを比較し、押下/解放の変化だけを表示します。
      //     buttonMask はマウスが宣言する全ボタンを持ちます（ゲーミングマウスでは
      //     boot reportで表せる3つより多いことがよくあります）。
      uint16_t pressed  = event.buttonMask & ~event.previousButtonMask;
      uint16_t released = event.previousButtonMask & ~event.buttonMask;
      static const char *names[3] = {"left  ", "right ", "middle"};
      uint8_t count = event.buttonCount ? event.buttonCount : 3;
      for (uint8_t i = 0; i < count; i++)
      {
        uint16_t bit = (uint16_t)(1u << i);
        if (!((pressed | released) & bit)) continue;
        if (i < 3) Serial.printf("%s %s\n", names[i], (pressed & bit) ? "press" : "release");
        else       Serial.printf("button%-2u %s\n", i + 1, (pressed & bit) ? "press" : "release");
      }
    }
    if (event.moved)
    {
      // en: Mouse reports relative deltas; accumulate them to show a simple position.
      //     pan is the horizontal wheel (AC Pan), 0 on a mouse that has none.
      // ja: マウスは相対移動量を報告するため、累積して簡易的な位置として表示します。
      //     pan は水平ホイール（AC Pan）で、非対応のマウスでは常に0です。
      posX += event.x;
      posY += event.y;
      Serial.printf("pos: x=%ld y=%ld  delta: dx=%d dy=%d wheel=%d pan=%d\n",
                    (long)posX, (long)posY,
                    (int)event.x, (int)event.y, (int)event.wheel, (int)event.pan);
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
