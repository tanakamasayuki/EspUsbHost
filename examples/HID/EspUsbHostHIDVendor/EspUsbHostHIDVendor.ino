// en: Receive vendor HID input and send matching output or feature reports.
// ja: Vendor HID入力を受け、対応するOutput・Feature reportを送る。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbHost.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;

// USBHIDVendor declares Report ID 6; interrupt OUT includes it on the wire.
static const uint8_t OUTPUT_REPORT[64] = {
    ESP_USB_HOST_HID_REPORT_ID_VENDOR, 'h', 'o', 's', 't', ' ', 'o', 'u', 't', 'p', 'u', 't'};
static const uint8_t FEATURE_REPORT[63] = {
    'h', 'o', 's', 't', ' ', 'f', 'e', 'a', 't', 'u', 'r', 'e'};

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(500);

  Serial.println("EspUsbHost HID vendor example start");

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
  usb.onHIDVendorInput([](const EspUsbHostHIDVendorInput &input)
                    {
    Serial.printf("vendor iface=%u len=%u data=", input.interfaceNumber, (unsigned)input.reportLength);
    // en: This peer sends a NUL-terminated text payload inside a fixed-size report.
    // ja: この相手デバイスは固定長レポート内にNUL終端テキストを入れて送ります。
    for (size_t i = 0; i < input.reportLength && input.reportData[i] != 0; i++)
    {
      Serial.write(input.reportData[i]);
    }
    Serial.println(); });

  // en: Start host processing; success does not mean a peripheral has finished enumeration.
  // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (Serial.available() > 0)
  {
    // en: Serial commands exercise HID Output and Feature reports separately.
    // ja: シリアルコマンドでHID OutputレポートとFeatureレポートを個別に試します。
    char command = Serial.read();
    if (command == 'o')
    {
      Serial.printf("send output: %s\n", usb.sendHIDVendorOutput(OUTPUT_REPORT, sizeof(OUTPUT_REPORT)) ? "ok" : "failed");
    }
    else if (command == 'f')
    {
      Serial.printf("send feature: %s\n", usb.sendHIDVendorFeature(FEATURE_REPORT, sizeof(FEATURE_REPORT)) ? "ok" : "failed");
    }
  }
  delay(1);
}
