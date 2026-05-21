# EspUsbHostGamepad

> English: [README.md](README.md)

USB HIDゲームパッドのレポートを受け取り、生のレポートバイトとHIDレポートディスクリプタから切り出したフィールドをシリアルモニターに表示するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB HIDゲームパッドまたはジョイスティック

## 動作内容

- パース対象レポートバイトと、レポートディスクリプタからデコードしたHIDフィールドを表示
- 軸の割り当て、スケーリング、デッドゾーン、デバイス固有マッピングはアプリケーション側で行う
- Generic DesktopのX/YやButton usageをアプリケーション用構造体へ詰め替える例を含む

## 主要API

- `usb.onGamepad(callback)` — ゲームパッドレポートごとに`EspUsbHostGamepadEvent`付きで呼ばれる
  - `event.fields`, `event.fieldCount` — レポートディスクリプタからデコードしたHID入力フィールド
  - `field.usagePage`, `field.usage` — Generic Desktopの`X`（`0x01:0x30`）やButton 1（`0x09:0x01`）などのHID usage
  - `field.value`, `field.logicalMin`, `field.logicalMax` — デコード済みの値と論理範囲
  - `field.bitOffset`, `field.bitSize`, `field.flags` — 入力レポート内の位置とInput item flags
  - `event.rawData`, `event.rawLength` — 生のHID入力レポートバイト
  - `event.reportData`, `event.reportLength` — Report IDがある場合はそれを除いたゲームパッドレポートバイト

## シリアル出力例

```
connected: device: address=1 portId=0x01 vid=054c pid=09cc class=0x00(Device) speed=full product="Wireless Controller"
report[11]=0A F6 14 EC 1E E2 03 05 00 00 00
  field[0] page=0x0001 usage=0x0030 value=10 logical=-127..127 bits=8@0 flags=0x02
  field[1] page=0x0001 usage=0x0031 value=-10 logical=-127..127 bits=8@8 flags=0x02
  field[7] page=0x0009 usage=0x0001 value=1 logical=0..1 bits=1@56 flags=0x02
```

> **注意:** 軸の順番、軸の意味、ボタン配置はデバイスやHIDレポートディスクリプタによって異なります。
> `onGamepad()`は軸を左/右スティックのような意味名に割り当てず、値の正規化もしません。`event.fields`、`event.rawData` / `event.rawLength`、`event.reportData` / `event.reportLength`で実際のレポートを確認し、16ビット軸や12ビットセンサー値を含めてVID/PIDごとのマッピングを追加してください。
