// en: Decode mouse motion and individual button transitions.
// ja: マウスの移動と各ボタンの押下・解放を読み解く。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbHost.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;

static int32_t posX = 0;
static int32_t posY = 0;

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(500);

  // en: Register the connection notification before startup; the reported address belongs to this enumeration.
  // ja: 開始前に接続通知を登録する。通知のaddressは今回の列挙で割り当てられたもの。
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.print("connected: ");
                          espUsbHostPrint(device); });

  // en: Use disconnect notifications to invalidate application state; a reconnect can have a new address.
  // ja: 切断通知を手元の状態の無効化に使う。再接続後のaddressは変わる場合がある。
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(device); });

  // en: Handle input on the USB client task; copy any borrowed report data needed after return.
  // ja: 入力はUSB clientタスクで受ける。戻った後も使うreportデータはコピーする。
  usb.onMouse([](const EspUsbHostMouseEvent &event)
              {
    if (event.buttonsChanged)
    {
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
      // en: Mouse reports relative deltas; accumulate them to show a simple position. pan is the horizontal wheel (AC Pan), 0 on a mouse that has none.
      // ja: マウスは相対移動量を報告するため、累積して簡易的な位置として表示します。 pan は水平ホイール（AC Pan）で、非対応のマウスでは常に0です。
      posX += event.x;
      posY += event.y;
      Serial.printf("pos: x=%ld y=%ld  delta: dx=%d dy=%d wheel=%d pan=%d\n",
                    (long)posX, (long)posY,
                    (int)event.x, (int)event.y, (int)event.wheel, (int)event.pan);
    } });

  // en: Start host processing; success does not mean a peripheral has finished enumeration.
  // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(1);
}
