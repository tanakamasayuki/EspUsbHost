// en: Inspect keyboard usages, modifiers and translated characters.
// ja: キーボードのusage・修飾キー・変換後の文字を調べる。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbHost.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;

static void printModifiers(uint8_t modifiers)
{
  // en: Modifier bits are printed as a compact '+' separated list.
  // ja: モディファイアのビット列を、'+'区切りの短い一覧として表示します。
  static const char *modifierNames[] = {
      "LCTRL", "LSHIFT", "LALT", "LGUI",
      "RCTRL", "RSHIFT", "RALT", "RGUI"};

  if (modifiers == 0)
  {
    Serial.print("none");
    return;
  }

  bool first = true;
  for (int i = 0; i < 8; i++)
  {
    if (modifiers & (1 << i))
    {
      if (!first)
      {
        Serial.print('+');
      }
      Serial.print(modifierNames[i]);
      first = false;
    }
  }
}

static void onKeyboard(const EspUsbHostKeyboardEvent &event)
{
  // en: Dump parsed keyboard fields instead of only printable characters.
  // ja: 表示可能文字だけでなく、パース済みキーボード情報をそのまま表示します。
  const char displayChar = (event.ascii >= 0x20 && event.ascii != 0x7F) ? static_cast<char>(event.ascii) : '.';

  Serial.printf("[%s] keycode=0x%02x ascii=0x%02x(%c) modifiers=",
                event.pressed ? "press  " : "release",
                event.keycode,
                event.ascii,
                displayChar);
  printModifiers(event.modifiers);
  Serial.println();
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(500);

  usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP);

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
  usb.onKeyboard(onKeyboard);

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
