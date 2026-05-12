#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostCdcSerial CdcSerial(usb);

void setup()
{
  Serial.begin(115200);
  delay(5000);

  Serial.println("EspUsbHost USB serial example start");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &info)
                        { Serial.printf("connected: vid=%04x pid=%04x product=%s\n", info.vid, info.pid, info.product); });

  CdcSerial.begin(115200);

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
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
