# EspUsbHostGamepad

USB HIDゲームパッドのスティック・十字キー・ボタン入力を受け取り、シリアルモニターに表示するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB HIDゲームパッドまたはジョイスティック

## 動作内容

- デバイスからレポートを受け取るたびに、すべてのゲームパッドイベントフィールドを表示

## 主要API

- `usb.onGamepad(callback)` — ゲームパッドレポートごとに`EspUsbHostGamepadEvent`付きで呼ばれる
  - `event.x`, `event.y` — 左スティック軸
  - `event.z`, `event.rz` — 右スティック軸（スロットル・ラダーの場合もある）
  - `event.rx`, `event.ry` — 追加軸
  - `event.hat` — ハット（十字キー）方向（0〜8、0=中央）
  - `event.buttons` — 32ビットボタンビットマスク（現在の状態）
  - `event.previousButtons` — 前回のボタンビットマスク

## シリアル出力例

```
connected: vid=054c pid=09cc product=Wireless Controller
gamepad x=0 y=0 z=128 rz=128 rx=0 ry=0 hat=0 buttons=0x00000000 previous=0x00000000
gamepad x=0 y=0 z=128 rz=128 rx=0 ry=0 hat=0 buttons=0x00000001 previous=0x00000000
```

> **注意:** 軸の範囲やボタン配置はデバイスやHIDレポートディスクリプタによって異なります。
> ブートゲームパッドレポート形式に準拠していないデバイスは正しく認識されない場合があります。
