// en: Read keyboard, mouse and media functions from a composite HID device.
// ja: 複合HID機器のキーボード・マウス・メディア入力を読む。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbHost.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;

static int32_t posX = 0;
static int32_t posY = 0;

static void onKeyboard(const EspUsbHostKeyboardEvent &event)
{
  if (!event.pressed)
  {
    return;
  }

  Serial.printf("keyboard: address=%u iface=%u keycode=0x%02x",
                event.address,
                event.interfaceNumber,
                event.keycode);

  if (event.ascii == '\r')
  {
    Serial.print(" ascii=Enter");
  }
  else if (event.ascii >= 0x20 && event.ascii != 0x7f)
  {
    Serial.printf(" ascii='%c'", (char)event.ascii);
  }

  Serial.println();
}

static void onMouse(const EspUsbHostMouseEvent &event)
{
  if (event.buttonsChanged)
  {
    uint8_t pressed = event.buttons & ~event.previousButtons;
    uint8_t released = event.previousButtons & ~event.buttons;
    if (pressed & ESP_USB_HOST_MOUSE_LEFT)
      Serial.printf("mouse: address=%u iface=%u left press\n", event.address, event.interfaceNumber);
    if (released & ESP_USB_HOST_MOUSE_LEFT)
      Serial.printf("mouse: address=%u iface=%u left release\n", event.address, event.interfaceNumber);
    if (pressed & ESP_USB_HOST_MOUSE_RIGHT)
      Serial.printf("mouse: address=%u iface=%u right press\n", event.address, event.interfaceNumber);
    if (released & ESP_USB_HOST_MOUSE_RIGHT)
      Serial.printf("mouse: address=%u iface=%u right release\n", event.address, event.interfaceNumber);
    if (pressed & ESP_USB_HOST_MOUSE_MIDDLE)
      Serial.printf("mouse: address=%u iface=%u middle press\n", event.address, event.interfaceNumber);
    if (released & ESP_USB_HOST_MOUSE_MIDDLE)
      Serial.printf("mouse: address=%u iface=%u middle release\n", event.address, event.interfaceNumber);
  }

  if (event.moved)
  {
    posX += event.x;
    posY += event.y;
    Serial.printf("mouse: address=%u iface=%u pos: x=%ld y=%ld delta: dx=%d dy=%d wheel=%d\n",
                  event.address,
                  event.interfaceNumber,
                  (long)posX,
                  (long)posY,
                  (int)event.x,
                  (int)event.y,
                  (int)event.wheel);
  }
}

static void onConsumerControl(const EspUsbHostConsumerControlEvent &event)
{
  Serial.printf("consumer: address=%u iface=%u %s usage=0x%04x %s\n",
                event.address,
                event.interfaceNumber,
                event.pressed ? "press" : "release",
                event.usage,
                espUsbHostConsumerControlUsageName(event.usage));
}

static void onSystemControl(const EspUsbHostSystemControlEvent &event)
{
  Serial.printf("system: address=%u iface=%u %s usage=0x%02x %s\n",
                event.address,
                event.interfaceNumber,
                event.pressed ? "press" : "release",
                event.usage,
                espUsbHostSystemControlUsageName(event.usage));
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(500);

  Serial.println("EspUsbHost multiple HID example start");

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
  usb.onMouse(onMouse);
  usb.onConsumerControl(onConsumerControl);
  usb.onSystemControl(onSystemControl);

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
