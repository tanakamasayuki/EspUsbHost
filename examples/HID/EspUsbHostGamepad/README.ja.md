# EspUsbHostGamepad

> English: [README.md](README.md)

USB HIDゲームパッドのレポートを受け取り、生のレポートバイトと簡易的なhat/ボタン候補をシリアルモニターに表示するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB HIDゲームパッドまたはジョイスティック

## 動作内容

- パース対象レポートバイトを軸として解釈せずに表示
- スケーリング、デッドゾーン、複数バイトのセンサー/軸解釈はアプリケーション側で行う
- hat候補がある場合はその値をそのまま表示
- ボタンの押下・解放デルタを現在のビットマスクと並べて表示

## 主要API

- `usb.onGamepad(callback)` — ゲームパッドレポートごとに`EspUsbHostGamepadEvent`付きで呼ばれる
  - `event.hat` — 生のhat候補値
  - `event.hasHat` — パース対象レポートにhat候補があったときtrue
  - `event.buttons` — 32ビットボタンビットマスク（現在の状態）
  - `event.previousButtons` — 前回のボタンビットマスク
  - `event.rawData`, `event.rawLength` — 生のHID入力レポートバイト
  - `event.reportData`, `event.reportLength` — Report IDがある場合はそれを除いたゲームパッドレポートバイト

## シリアル出力例

```
connected: device: address=1 portId=0x01 vid=054c pid=09cc class=0x00(Device) speed=full product="Wireless Controller"
report[11]=00 00 00 00 00 00 00 00 00 00 00 hat=0 buttons=0x00000000
report[11]=0A F6 14 EC 1E E2 03 05 00 00 00 hat=3 buttons=0x00000005 +0x5
```

> **注意:** 軸の順番、軸の意味、ボタン配置はデバイスやHIDレポートディスクリプタによって異なります。
> `onGamepad()`は軸を左/右スティックのような意味名に割り当てず、値の正規化もしません。`event.rawData` / `event.rawLength`、`event.reportData` / `event.reportLength`で実際のレポートを確認し、16ビット軸や12ビットセンサー値を含めてデバイスごとのマッピングを追加してください。
