# EspUsbHostHIDRawDump

> English: [README.md](README.md)

接続されたすべてのデバイスから、デバイスアドレス・インターフェース・プロトコル情報付きで生のHID入力レポートをダンプするサンプルです。
複数のUSBデバイスを同時に扱えます。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB HIDデバイス1台以上（キーボード・マウス・ゲームパッドなど）

## 動作内容

- 接続・切断時にデバイスアドレス・VID・PID・製品名を表示
- 生のHID入力レポートをスペース区切りのHexでダンプし、アドレスとインターフェース情報も付与

## 主要API

- `usb.onHIDInput(callback)` — HID入力レポートごとに`EspUsbHostHIDInput`付きで呼ばれる
  - `input.address` — USBデバイスアドレス（複数デバイスの区別に使用）
  - `input.vid`, `input.pid` — デバイス別処理に使えるUSBベンダーID/プロダクトID
  - `input.product` — 取得できた場合のUSBプロダクト文字列
  - `input.interfaceNumber` — HIDインターフェース番号
  - `input.subclass` — HIDサブクラス（0=なし、1=ブートインターフェース）
  - `input.protocol` — HIDプロトコル（0=なし、1=キーボード、2=マウス）
  - `input.length` — レポートのバイト長
  - `input.data` — 生のレポートバイト列
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`
  - `device.address` — USBデバイスアドレス
- `espUsbHostPrint(input)` / `espUsbHostPrint(device)` — このサンプルで使っている1行シリアルダンプ用ヘルパー

## シリアル出力例

```
connected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
hid: address=1 iface=0 subclass=0x01 protocol=0x01 len=8 data=00 00 00 00 00 00 00 00
hid: address=1 iface=0 subclass=0x01 protocol=0x01 len=8 data=00 00 04 00 00 00 00 00
disconnected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
```

## 関連サンプル

- [EspUsbHostDeviceInfo](../../Info/EspUsbHostDeviceInfo/) — デバイス・インターフェース・エンドポイントの詳細表示
