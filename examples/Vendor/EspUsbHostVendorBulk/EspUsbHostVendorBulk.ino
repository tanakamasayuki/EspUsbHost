#include "EspUsbHost.h"

EspUsbHost usb;

static uint8_t deviceAddress = 0;
static bool connected = false;

void setup()
{
  // en: Event prints can burst faster than the default TX buffer drains.
  // ja: イベント出力が既定のTXバッファ排出より速くバーストすることがあります。
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  delay(500);

  Serial.println("EspUsbHost vendor bulk/control example start");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          deviceAddress = device.address;
                          connected = true;
                          Serial.print("connected: ");
                          espUsbHostPrint(device);
                          // en: Claim the vendor-specific interface and start bulk IN reception.
                          // ja: vendor-specificインターフェースをclaimし、bulk IN受信を開始します。
                          Serial.printf("vendorOpen: %s\n", usb.vendorOpen(deviceAddress) ? "ok" : "failed"); });

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
  }
  delay(1);
}
