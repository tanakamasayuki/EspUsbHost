#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostCdcSerial CdcSerial(usb);

void setup()
{
  Serial.begin(115200);
  delay(5000);

  Serial.println("EspUsbHost USB serial example start");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.print("connected: ");
                          espUsbHostPrint(device); });

  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             Serial.print("disconnected: ");
                             espUsbHostPrint(device); });

  // en: The receive ring is 512 bytes by default and silently drops the oldest
  //     byte when it overflows. At 921600 baud that is only about 5.5 ms of
  //     traffic, so enlarge it here if the device is fast or sends in bursts.
  //     It must be called before begin(), and returns false if it fails.
  // ja: 受信リングは既定512バイトで、溢れると古いバイトから黙って捨てます。
  //     921600 baudでは約5.5ms分しかないため、高速なデバイスやバースト送信の
  //     デバイスではここで拡張します。begin()より前に呼ぶ必要があり、失敗時は
  //     falseを返します。
  // CdcSerial.setRxBufferSize(8192);

  CdcSerial.begin(115200);

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  // en: Bridge data in both directions between USB CDC and the board Serial port.
  // ja: USB CDCとボードのSerialポート間で、双方向にデータを中継します。
  while (CdcSerial.available() > 0)
  {
    Serial.write(CdcSerial.read());
  }

  while (Serial.available() > 0)
  {
    CdcSerial.write(Serial.read());
  }
  delay(1);
}
