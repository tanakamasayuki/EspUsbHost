# EspUsbHostCompositeHID

> English: [README.md](README.md)

1台の複合USB HIDデバイス、またはUSBハブ経由で接続された複数のUSB HIDデバイスから、複数種類のHID入力を同時に読むサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- 1台の複合USB HIDデバイス、または複数のUSB HIDデバイス。例:
  - トラックパッド付きキーボード
  - メディアキー付きキーボード
  - キーボードとマウスのインターフェースを持つワイヤレスレシーバー
  - USBハブ経由で接続した個別のUSBキーボードとUSBマウス

## 動作内容

- 1つの `EspUsbHost` インスタンスに複数のパース済みHIDコールバックを登録
- キーボードのキー押下を、デバイスアドレスとインターフェース番号付きで表示
- マウス／トラックパッドの移動、ボタン変化、ホイール移動を表示
- 音量や再生キーなどのConsumer Control usageを表示
- Power、Standby、WakeなどのSystem Control usageを表示
- `event.address` と `event.interfaceNumber` で、どのデバイス／インターフェースからのイベントか確認

このサンプルは入力レポートに絞っています。キーボードLEDなどの出力レポートは、デバイスごとのReport IDや出力レポート構造に依存しやすいため含めていません。

## 主要API

- `usb.onKeyboard(callback)` — キーボードのキー押下・解放イベントで呼ばれる
- `usb.onMouse(callback)` — マウスまたはトラックパッドのイベントで呼ばれる
- `usb.onConsumerControl(callback)` — メディアキーなどのConsumer Page操作で呼ばれる
- `usb.onSystemControl(callback)` — System Control usageで呼ばれる
- `event.address` / `event.interfaceNumber` — どのデバイス／インターフェースからイベントが来たか確認するときに便利
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`

## シリアル出力例

```
EspUsbHost multiple HID example start
connected: device: address=1 portId=0x01 vid=05ac pid=0262 class=0x00(Device) speed=full product="Keyboard with Trackpad"
keyboard: address=1 iface=0 keycode=0x04 ascii='a'
mouse: address=1 iface=1 pos: x=3 y=-2 delta: dx=3 dy=-2 wheel=0
consumer: address=1 iface=2 press usage=0x00e9 Volume Up
consumer: address=1 iface=2 release usage=0x00e9 Volume Up
system: address=1 iface=3 press usage=0x82 Standby
```

## 関連

- [EspUsbHostKeyboard](../EspUsbHostKeyboard/) — 基本的なキーボード文字入力
- [EspUsbHostMouse](../EspUsbHostMouse/) — 基本的なマウス移動・ボタン処理
- [EspUsbHostConsumerControl](../EspUsbHostConsumerControl/) — メディアキー処理
- [EspUsbHostSystemControl](../EspUsbHostSystemControl/) — 電源／スリープ／復帰処理
- [EspUsbHostHIDRawDump](../EspUsbHostHIDRawDump/) — トラブルシュート用の生HIDレポート表示
