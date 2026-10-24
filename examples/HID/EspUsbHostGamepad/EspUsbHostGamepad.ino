// en: Inspect gamepad fields using their HID usages.
// ja: ゲームパッドの各fieldをHID usageから調べる。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbHost.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;

static void printHex(const uint8_t *data, size_t length)
{
  for (size_t i = 0; i < length; i++)
  {
    if (data[i] < 0x10)
    {
      Serial.print('0');
    }
    Serial.print(data[i], HEX);
    if (i + 1 < length)
    {
      Serial.print(' ');
    }
  }
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(500);

  Serial.println("EspUsbHost gamepad example start");

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
  usb.onGamepad([](const EspUsbHostGamepadEvent &event)
                {
    Serial.printf("report[%u]=", (unsigned)event.reportLength);
    printHex(event.reportData, event.reportLength);
    Serial.println();

    for (size_t i = 0; i < event.fieldCount; i++)
    {
      const EspUsbHostHIDFieldValue &field = event.fields[i];
      Serial.printf("  field[%u] page=0x%04x usage=0x%04x value=%ld logical=%ld..%ld bits=%u@%u flags=0x%02x\n",
                    (unsigned)i,
                    field.usagePage,
                    field.usage,
                    (long)field.value,
                    (long)field.logicalMin,
                    (long)field.logicalMax,
                    field.bitSize,
                    field.bitOffset,
                    field.flags);
    }

    // en: Example mapping. Replace VID/PID and usage choices with your controller.
    // ja: マッピング例です。VID/PIDとusageの選択はコントローラーに合わせて置き換えてください。
    if (event.vid == 0x054c)
    {
      int32_t lx = 0;
      int32_t ly = 0;
      uint32_t buttons = 0;
      for (size_t i = 0; i < event.fieldCount; i++)
      {
        const EspUsbHostHIDFieldValue &field = event.fields[i];
        if (field.usagePage == 0x01 && field.usage == 0x30)
        {
          lx = field.value;
        }
        else if (field.usagePage == 0x01 && field.usage == 0x31)
        {
          ly = field.value;
        }
        else if (field.usagePage == 0x09 && field.value && field.usage > 0 && field.usage <= 32)
        {
          buttons |= 1UL << (field.usage - 1);
        }
      }
      Serial.printf("  mapped lx=%ld ly=%ld buttons=0x%08lx\n",
                    (long)lx,
                    (long)ly,
                    (unsigned long)buttons);
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
