// en: Open a matching interface and exercise vendor bulk and control transfers.
// ja: 対応interfaceを開き、Vendor bulkとcontrol転送を試す。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbHost.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;

static uint8_t deviceAddress = 0;
static bool connected = false;

void setup()
{
  // en: Event prints can burst faster than the default TX buffer drains.
  // ja: イベント出力が既定のTXバッファ排出より速くバーストすることがあります。
  Serial.setTxBufferSize(4096);
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(500);

  Serial.println("EspUsbHost vendor bulk/control example start");

  // en: Register the connection notification before startup; the reported address belongs to this enumeration.
  // ja: 開始前に接続通知を登録する。通知のaddressは今回の列挙で割り当てられたもの。
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          deviceAddress = device.address;
                          connected = true;
                          Serial.print("connected: ");
                          espUsbHostPrint(device);
                          // en: Claim the vendor-specific interface and start bulk IN reception.
                          // ja: vendor-specificインターフェースをclaimし、bulk IN受信を開始します。
                          Serial.printf("vendorOpen: %s\n", usb.vendorOpen(deviceAddress) ? "ok" : "failed"); });

  // en: Use disconnect notifications to invalidate application state; a reconnect can have a new address.
  // ja: 切断通知を手元の状態の無効化に使う。再接続後のaddressは変わる場合がある。
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             connected = false;
                             Serial.print("disconnected: ");
                             espUsbHostPrint(device); });

  // en: Bulk IN payloads arrive here. The data pointer is valid only during the callback.
  // ja: bulk INのペイロードはここに届きます。dataポインタはコールバック中のみ有効です。
  usb.onVendorData([](const EspUsbHostVendorData &data)
                   {
                     Serial.printf("vendor in iface=%u ep=0x%02x len=%u data=",
                                   data.interfaceNumber, data.endpoint, (unsigned)data.length);
                     for (size_t i = 0; i < data.length; i++)
                     {
                       Serial.write(data.data[i]);
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
  if (connected && Serial.available() > 0)
  {
    char command = Serial.read();
    if (command == 'w')
    {
      // en: Bulk OUT. The peer device echoes it back as a bulk IN payload.
      // ja: bulk OUT。相手デバイスはbulk INのペイロードとしてエコーバックします。
      static const uint8_t payload[] = {'p', 'i', 'n', 'g'};
      Serial.printf("bulk write: %s\n", usb.vendorWrite(payload, sizeof(payload), deviceAddress) ? "ok" : "failed");
    }
    else if (command == 'r')
    {
      // en: Non-blocking read from the per-device receive buffer.
      // ja: デバイスごとの受信バッファからのノンブロッキング読み出し。
      uint8_t buffer[64] = {};
      size_t length = usb.vendorRead(buffer, sizeof(buffer) - 1, deviceAddress);
      Serial.printf("bulk read: len=%u data=%s\n", (unsigned)length, (const char *)buffer);
    }
    else if (command == 'c')
    {
      // en: EP0 vendor control IN (bmRequestType=0xc0). The peer returns its name.
      // ja: EP0 vendor control IN（bmRequestType=0xc0）。相手デバイスは自身の名前を返します。
      uint8_t buffer[64] = {};
      size_t actual = 0;
      bool ok = usb.vendorControlIn(0x01, 0, 0, buffer, sizeof(buffer) - 1, &actual, deviceAddress);
      Serial.printf("control in: %s len=%u data=%s\n", ok ? "ok" : "failed", (unsigned)actual, (const char *)buffer);
    }
    else if (command == 'o')
    {
      // en: EP0 vendor control OUT (bmRequestType=0x40), zero-length data stage.
      // ja: EP0 vendor control OUT（bmRequestType=0x40）、データステージ長ゼロ。
      bool ok = usb.vendorControlOut(0x02, 0, 0, nullptr, 0, deviceAddress);
      Serial.printf("control out: %s\n", ok ? "ok" : "failed");
    }
    else if (command == 'q')
    {
      // en: For a device that streams: keep two 8 KB IN transfers in flight instead of one packet at a time. Data still arrives through onVendorData().
      // ja: 流しっぱなしのデバイス向け。1パケットずつではなく8 KBのIN転送を2本 同時に飛ばし続けます。データは従来どおりonVendorData()に届きます。
      bool ok = usb.vendorReadQueueBegin(2, 8192, deviceAddress);
      Serial.printf("read queue: %s transfer=%u\n",
                    ok ? "ok" : "failed",
                    (unsigned)usb.vendorInTransferBytes(deviceAddress));
    }
    else if (command == 'e')
    {
      // en: Stop the queue. The endpoint goes idle; vendorOpen() starts the continuous read again.
      // ja: キューを止めます。endpointはアイドルになり、vendorOpen()で継続受信に戻ります。
      usb.vendorReadQueueEnd(deviceAddress);
      const EspUsbHostVendorReadStats stats = usb.vendorReadStats(deviceAddress);
      Serial.printf("read queue ended: completed=%lu bytes=%llu errors=%lu starved=%lu\n",
                    (unsigned long)stats.completed,
                    (unsigned long long)stats.bytes,
                    (unsigned long)stats.errors,
                    (unsigned long)stats.starved);
    }
  }
  delay(1);
}
