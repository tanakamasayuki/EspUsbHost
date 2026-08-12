#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostCdcSerial FtdiSerial(usb);
EspUsbHostCdcSerial Cp210xSerial(usb);

static constexpr uint16_t VID_FTDI = 0x0403;
static constexpr uint16_t VID_CP210X = 0x10c4;

static void onDeviceConnected(const EspUsbHostDeviceInfo &device)
{
  Serial.print("connected: ");
  espUsbHostPrint(device);

  // en: Bind each serial wrapper to the matching USB device address.
  // ja: 各シリアルラッパーを、対応するUSBデバイスアドレスに割り当てます。
  if (device.vid == VID_FTDI)
  {
    FtdiSerial.setAddress(device.address);
    Serial.printf("FTDI assigned: portId=0x%02x address=%u serial=%s\n",
                  device.portId,
                  device.address,
                  device.serial);
  }
  else if (device.vid == VID_CP210X)
  {
    Cp210xSerial.setAddress(device.address);
    Serial.printf("CP210x assigned: portId=0x%02x address=%u serial=%s\n",
                  device.portId,
                  device.address,
                  device.serial);
  }
}

static void onDeviceDisconnected(const EspUsbHostDeviceInfo &device)
{
  Serial.print("disconnected: ");
  espUsbHostPrint(device);

  // en: Clear only the wrapper that was bound to the removed device.
  // ja: 取り外されたデバイスに割り当てていたラッパーだけを解除します。
  if (FtdiSerial.address() == device.address)
  {
    FtdiSerial.setAddress(0);
    Serial.println("FTDI released");
  }
  if (Cp210xSerial.address() == device.address)
  {
    Cp210xSerial.setAddress(0);
    Serial.println("CP210x released");
  }
}

static void forwardUsbToSerial(EspUsbHostCdcSerial &usbSerial, const char *label)
{
  // en: Drain one USB serial stream and label its bytes in the Serial Monitor.
  // ja: 1つのUSBシリアルストリームを読み出し、シリアルモニター上でラベル付き表示します。
  while (usbSerial.connected() && usbSerial.available() > 0)
  {
    Serial.printf("%s: ", label);
    while (usbSerial.available() > 0)
    {
      Serial.write(usbSerial.read());
    }
    Serial.println();
  }
}

void setup()
{
  Serial.begin(115200);
  delay(5000);
  Serial.println("EspUsbHost multi USB serial example start");
  Serial.println("Connect one FTDI device and one CP210x device.");
  Serial.println("Serial Monitor input is sent to both devices.");

  // en: Each wrapper owns its own 512-byte receive ring, which drops the oldest
  //     byte on overflow. The size is per instance, so only the device that
  //     actually needs more has to pay for it. Call it before begin().
  // ja: 受信リングはラッパーごとに独立していて（既定512バイト）、溢れると古い
  //     バイトから捨てます。サイズはインスタンス単位なので、必要なデバイスの
  //     分だけ増やせます。begin()より前に呼びます。
  // FtdiSerial.setRxBufferSize(8192);

  FtdiSerial.begin(115200);
  Cp210xSerial.begin(115200);
  FtdiSerial.setAddress(0);
  Cp210xSerial.setAddress(0);

  usb.onDeviceConnected(onDeviceConnected);
  usb.onDeviceDisconnected(onDeviceDisconnected);

  if (!usb.begin())
  {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop()
{
  // en: Broadcast Serial Monitor input to all currently connected USB serial devices.
  // ja: シリアルモニターからの入力を、現在接続中のUSBシリアルデバイスすべてへ送ります。
  while (Serial.available() > 0)
  {
    const int c = Serial.read();
    if (FtdiSerial.connected())
    {
      FtdiSerial.write(static_cast<uint8_t>(c));
    }
    if (Cp210xSerial.connected())
    {
      Cp210xSerial.write(static_cast<uint8_t>(c));
    }
  }

  forwardUsbToSerial(FtdiSerial, "FTDI");
  forwardUsbToSerial(Cp210xSerial, "CP210x");
  delay(1);
}
