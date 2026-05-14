# EspUsbHostMouse

> English: [README.md](README.md)

USB HIDマウスの移動量・ボタン操作を受け取り、累積座標の追跡とクリック検出を行うサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB HIDマウス

## 動作内容

- 移動デルタを `posX` / `posY` に累積し、常に絶対座標として参照できる
- ボタンごとの押下・解放を立ち上がり／立ち下がりエッジで検出
- 移動イベントごとに累積座標とデルタを表示

## 主要API

- `usb.onMouse(callback)` — マウスイベントごとに`EspUsbHostMouseEvent`付きで呼ばれる
  - `event.buttonsChanged` — ボタン状態が変化したときtrue
  - `event.moved` — 位置が変化したときtrue
  - `event.x`, `event.y`, `event.wheel` — 相対移動量（`int16_t`）
  - `event.buttons`, `event.previousButtons` — ボタンのビットマスク（`uint8_t`）

## シリアル出力例

```
connected: vid=046d pid=c52b product=USB Receiver
left   press
pos: x=3 y=-2  delta: dx=3 dy=-2 wheel=0
pos: x=3 y=-3  delta: dx=0 dy=-1 wheel=0
left   release
pos: x=3 y=-3  delta: dx=0 dy=0 wheel=-1
```
