# EspUsbHostMouse

USB HIDマウスの移動量・ボタン操作を受け取り、シリアルモニターに表示するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB HIDマウス

## 動作内容

- ボタンの押下・解放時にボタン状態の変化を表示
- マウスを動かすたびにX・Y・ホイールの移動量を表示

## 主要API

- `usb.onMouse(callback)` — マウスイベントごとに`EspUsbHostMouseEvent`付きで呼ばれる
  - `event.buttonsChanged` — ボタン状態が変化したときtrue
  - `event.moved` — 位置が変化したときtrue
  - `event.x`, `event.y`, `event.wheel` — 相対移動量
  - `event.buttons`, `event.previousButtons` — ボタンのビットマスク

## シリアル出力例

```
buttons: 0x00 -> 0x01
move: x=3 y=-2 wheel=0 buttons=0x01
move: x=0 y=0 wheel=-1 buttons=0x00
buttons: 0x01 -> 0x00
```
