// en: Capture the HID report descriptor used to interpret input bytes.
// ja: HID入力byteの解釈に使うreport descriptorを取得する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include <Arduino.h>
#include <EspUsbHost.h>

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;

static uint32_t reportDescriptorCount = 0;

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(5000);
  Serial.println("EspUsbHostHIDReportDescriptor start");
  Serial.println("Connect a HID device to print its HID report descriptor.");

  // en: Register the connection notification before startup; the reported address belongs to this enumeration.
  // ja: 開始前に接続通知を登録する。通知のaddressは今回の列挙で割り当てられたもの。
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.print("CONNECTED ");
                          espUsbHostPrint(device);
                          // en: Print the regular USB descriptor summary first.
                          // ja: 先に通常のUSBディスクリプタ概要を表示します。
                          usb.printDeviceInfo(device.address); });

  // en: Use disconnect notifications to invalidate application state; a reconnect can have a new address.
  // ja: 切断通知を手元の状態の無効化に使う。再接続後のaddressは変わる場合がある。
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             Serial.print("DISCONNECTED ");
                             espUsbHostPrint(device); });

  usb.onHIDReportDescriptor([](const EspUsbHostHIDReportDescriptor &descriptor)
                            {
                              reportDescriptorCount++;
                              Serial.printf("\n===== HID Report Descriptor #%lu =====\n",
                                            static_cast<unsigned long>(reportDescriptorCount));
                              espUsbHostPrint(descriptor);
                              Serial.println("=== HID Report Descriptor End ===\n"); });

  // en: Start host processing; success does not mean a peripheral has finished enumeration.
  // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  delay(10);
}
