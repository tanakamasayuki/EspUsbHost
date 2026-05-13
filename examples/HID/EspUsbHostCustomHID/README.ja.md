# EspUsbHostCustomHID

> English: [README.md](README.md)

接続された任意のHIDデバイスから生のHID入力レポートをHex形式でダンプするサンプルです。
未知のHIDデバイスのリバースエンジニアリングや、カスタムHIDパーサーの開発に役立ちます。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- 任意のUSB HIDデバイス（キーボード・マウス・ゲームパッド・カスタムデバイスなど）

## 動作内容

- デバイスの種類を問わず、すべてのHID入力レポートを受信
- 各レポートのインターフェース番号・サブクラス・プロトコル・バイト長・生のHexデータを表示

## 主要API

- `usb.onHIDInput(callback)` — HID入力レポートごとに`EspUsbHostHIDInput`付きで呼ばれる
  - `input.interfaceNumber` — HIDインターフェース番号
  - `input.subclass` — HIDサブクラス（0=なし、1=ブートインターフェース）
  - `input.protocol` — HIDプロトコル（0=なし、1=キーボード、2=マウス）
  - `input.length` — レポートのバイト長
  - `input.data` — 生のレポートバイト列

## シリアル出力例

```
connected: vid=045e pid=07a5 product=USB Keyboard
hid iface=0 subclass=1 protocol=1 len=8 data=0000000000000000
hid iface=0 subclass=1 protocol=1 len=8 data=0000040000000000
```

> **注意:** このサンプルは[EspUsbHostHIDRawDump](../EspUsbHostHIDRawDump/)と類似しています。HIDRawDumpはデバイスアドレスも表示し、Hexをスペース区切りで表示します。
