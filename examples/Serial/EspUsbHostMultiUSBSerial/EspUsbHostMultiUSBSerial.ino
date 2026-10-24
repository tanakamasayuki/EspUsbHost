// en: Bind separate serial wrappers to selected USB device addresses.
// ja: 複数のシリアルwrapperを相手のUSB addressへ結び付ける。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbHost.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
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
    // en: Read only bytes already buffered; application message boundaries must be tracked separately.
    // ja: bufferに届いたbyteを読む。アプリのメッセージの区切りは別に管理する。
    while (usbSerial.available() > 0)
    {
      Serial.write(usbSerial.read());
    }
    Serial.println();
  }
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(5000);
  Serial.println("EspUsbHost multi USB serial example start");
  Serial.println("Connect one FTDI device and one CP210x device.");
  Serial.println("Serial Monitor input is sent to both devices.");

  // FtdiSerial.setRxBufferSize(8192);

  FtdiSerial.begin(115200);
  Cp210xSerial.begin(115200);
  FtdiSerial.setAddress(0);
  Cp210xSerial.setAddress(0);

  // en: Register the connection notification before startup; the reported address belongs to this enumeration.
  // ja: 開始前に接続通知を登録する。通知のaddressは今回の列挙で割り当てられたもの。
  usb.onDeviceConnected(onDeviceConnected);
  // en: Use disconnect notifications to invalidate application state; a reconnect can have a new address.
  // ja: 切断通知を手元の状態の無効化に使う。再接続後のaddressは変わる場合がある。
  usb.onDeviceDisconnected(onDeviceDisconnected);

  // en: Start host processing; success does not mean a peripheral has finished enumeration.
  // ja: Hostの処理を開始する。成功しても相手機器の列挙完了を意味しない。
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
