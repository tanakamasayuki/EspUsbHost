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
