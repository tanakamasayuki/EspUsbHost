// en: Classify connected devices from their interface information.
// ja: interface情報を使って接続機器を分類する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include <Arduino.h>
#include <EspUsbHost.h>

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbHost usb;

static bool isKeyboardLike(const EspUsbHostDeviceInfo &device)
{
  EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
  const size_t count = usb.getInterfaces(device.address, interfaces, ESP_USB_HOST_MAX_INTERFACES);

  // en: Detect a boot keyboard-style HID interface by class/protocol.
  // ja: class/protocolから、ブートキーボード相当のHIDインターフェースを検出します。
  for (size_t i = 0; i < count; i++)
  {
    if (interfaces[i].interfaceClass == 0x03 && interfaces[i].interfaceProtocol == 0x01)
    {
      return true;
    }
  }
  return false;
}

static void onDeviceConnected(const EspUsbHostDeviceInfo &device)
{
  Serial.print("connected: ");
  espUsbHostPrint(device);

  if (isKeyboardLike(device))
  {
    Serial.println("  this device has a keyboard interface");
  }
  if (device.isHub)
  {
    Serial.println("  this device is a USB hub");
  }

  Serial.printf("  total devices: %u\n", static_cast<unsigned>(usb.deviceCount()));
}

static void onDeviceDisconnected(const EspUsbHostDeviceInfo &device)
{
  Serial.print("disconnected: ");
  espUsbHostPrint(device);
  Serial.printf("  total devices: %u\n", static_cast<unsigned>(usb.deviceCount()));
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(500);
  Serial.println("EspUsbHost custom device callbacks example start");
  Serial.println("Press 'd' to print the full descriptor dump.");

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
  if (Serial.available() > 0 && Serial.read() == 'd')
  {
    usb.printAllDeviceInfo();
  }
  delay(10);
}
