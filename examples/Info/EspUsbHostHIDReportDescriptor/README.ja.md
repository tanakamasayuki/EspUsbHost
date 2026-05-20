# EspUsbHostHIDReportDescriptor

> English: [README.md](README.md)

接続されたHIDデバイスのHIDレポートディスクリプタを表示するサンプルです。

通常のアプリケーションでレポートディスクリプタを毎回表示する必要はあまりありません。新しいHIDデバイスの調査、Report IDの確認、製品ごとに軸やボタン配置が異なるゲームパッド対応を設計するときに役立ちます。

## ハードウェア

- ESP32-S3、またはArduino-ESP32 USB Hostに対応したボード
- キーボード、マウス、ゲームパッド、カスタムHIDなどのUSB HIDデバイス

## 動作内容

- 接続時に通常のUSBデバイス情報を表示
- `onHIDReportDescriptor()`でHIDレポートディスクリプタを非同期に取得
- raw descriptor bytesを表示
- HID itemの簡易デコードを表示

## 主要API

- `usb.onHIDReportDescriptor(callback)` — 列挙後にHIDレポートディスクリプタを受け取る
- `espUsbHostPrint(descriptor)` — メタデータ、raw bytes、簡易item decodeを表示
- `espUsbHostPrintHIDReportDescriptor(data, length)` — raw descriptor bufferだけを表示

## シリアル出力例

```
EspUsbHostHIDReportDescriptor start
Connect a HID device to print its HID report descriptor.
CONNECTED address=1 vid=303a pid=4004 product="HID Device"

===== HID Report Descriptor #1 =====
hid report descriptor: address=1 iface=0 hid=0x0111 country=0x00 type=0x22 reported_len=38 len=38
Raw HID report descriptor: 05 01 09 04 A1 01 ...
HID report descriptor: len=38
  0000: 0x05 Global   Usage Page         value=1 raw=01
  0002: 0x09 Local    Usage              value=4 raw=04
  0004: 0xa1 Main     Collection         value=1 raw=01
=== HID Report Descriptor End ===
```
