// en: Enumerate devices and print their interfaces and endpoints.
// ja: 接続機器とinterface・endpointの情報を表示する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include <Arduino.h>
#include <EspUsbHost.h>

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;

static uint32_t lastDeviceEventMs = 0;
static uint32_t lastPrintMs = 0;
static constexpr uint32_t PRINT_INTERVAL_MS = 10000;

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(5000);
  Serial.println("EspUsbHostDeviceInfo start");
  Serial.println("Printing connected devices and endpoint channel estimates every 10 seconds.");

  // en: Register the connection notification before startup; the reported address belongs to this enumeration.
  // ja: 開始前に接続通知を登録する。通知のaddressは今回の列挙で割り当てられたもの。
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          lastDeviceEventMs = millis();
                          Serial.printf("CONNECTED address=%u\n", device.address); });

  // en: Use disconnect notifications to invalidate application state; a reconnect can have a new address.
  // ja: 切断通知を手元の状態の無効化に使う。再接続後のaddressは変わる場合がある。
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             lastDeviceEventMs = millis();
                             Serial.printf("DISCONNECTED address=%u vid=%04x pid=%04x\n",
                                           device.address,
                                           device.vid,
                                           device.pid); });

  // en: Start host processing; success does not mean a peripheral has finished enumeration.
  // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
  if (!usb.begin())
  {
    Serial.printf("usb.begin failed: %s\n", usb.lastErrorName());
  }
  lastDeviceEventMs = millis();
  lastPrintMs = millis() - PRINT_INTERVAL_MS;
}

void loop()
{
  const uint32_t now = millis();
  if (now - lastPrintMs >= PRINT_INTERVAL_MS && now - lastDeviceEventMs > 500)
  {
    lastPrintMs = now;
    usb.printAllDeviceInfo();
  }
  delay(10);
}
