# EspUsbHostGamepad

> English: [README.md](README.md)

USB HIDゲームパッドのスティック・十字キー・ボタン入力を受け取り、シリアルモニターに表示するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB HIDゲームパッドまたはジョイスティック

## 動作内容

- 軸値を −1.0〜1.0 に正規化（生の軸値は `int8_t`、範囲 −128〜127）
- デッドゾーン ±10 を適用 — この範囲内の値は 0.0 として報告
- ハット（十字キー）方向をコンパス表記（N・NE・E…）で表示
- ボタンの押下・解放デルタを現在のビットマスクと並べて表示

## 主要API

- `usb.onGamepad(callback)` — ゲームパッドレポートごとに`EspUsbHostGamepadEvent`付きで呼ばれる
  - `event.x`, `event.y` — 左スティック軸（`int8_t`）
  - `event.z`, `event.rz` — 右スティック軸（スロットル・ラダーの場合もある）
  - `event.rx`, `event.ry` — 追加軸
  - `event.hat` — ハット方向（0〜7 = N/NE/E/SE/S/SW/W/NW、その他=中央）
  - `event.buttons` — 32ビットボタンビットマスク（現在の状態）
  - `event.previousButtons` — 前回のボタンビットマスク

## シリアル出力例

```
connected: vid=054c pid=09cc product=Wireless Controller
x= 0.000 y= 0.000 z= 0.000 rz= 0.000 hat=- buttons=0x00000000
x= 0.000 y= 0.000 z= 0.000 rz= 0.000 hat=- buttons=0x00000001 +0x1
x= 0.000 y= 0.000 z= 0.000 rz= 0.000 hat=N buttons=0x00000001
```

> **注意:** 軸の範囲やボタン配置はデバイスやHIDレポートディスクリプタによって異なります。
> ブートゲームパッドレポート形式に準拠していないデバイスは正しく認識されない場合があります。
