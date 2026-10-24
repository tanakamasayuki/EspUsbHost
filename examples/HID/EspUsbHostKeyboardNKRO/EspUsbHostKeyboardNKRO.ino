// en: Observe held-key bitmaps and simultaneous key changes.
// ja: 保持キーのbitmapと複数キーの変化を観察する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbHost.h"


// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;

static int pressedCount = 0; // keys currently held
static int maxSimultaneous = 0;

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1000);

  usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US);

  // en: Register the connection notification before startup; the reported address belongs to this enumeration.
  // ja: 開始前に接続通知を登録する。通知のaddressは今回の列挙で割り当てられたもの。
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.printf("Keyboard connected: %04x:%04x nkro-bitmap=%u\n",
                                        device.vid, device.pid,
                                        usb.keyboardUsesBitmapReport(device.address) ? 1 : 0);
                        });

  // en: Handle input on the USB client task; copy any borrowed report data needed after return.
  // ja: 入力はUSB clientタスクで受ける。戻った後も使うreportデータはコピーする。
  usb.onKeyboardState([](const EspUsbHostKeyboardState &state)
                      {
                        pressedCount = 0;
                        for (uint16_t usage = 0; usage <= 0xff; usage++)
                        {
                          const uint8_t keycode = static_cast<uint8_t>(usage);
                          if (state.isDown(keycode))
                          {
                            pressedCount++;
                          }
                          if (state.wasPressed(keycode))
                          {
                            Serial.printf("press   keycode=0x%02x\n", keycode);
                          }
                          else if (state.wasReleased(keycode))
                          {
                            Serial.printf("release keycode=0x%02x\n", keycode);
                          }
                        }
                        if (pressedCount > maxSimultaneous)
                        {
                          maxSimultaneous = pressedCount;
                        }
                        Serial.printf("held=%d (max=%d) modifiers=0x%02x\n",
                                      pressedCount, maxSimultaneous, state.modifiers);
                      });

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
