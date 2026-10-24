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

## 読み方と確認の手順

### 実行前の準備

ゲームパッドの各fieldをHID usageから調べる。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`onGamepad()`では生byteとデコード後のfieldを見られます。軸やボタンを一つずつ操作し、usagePage・usage・field.valueを対応付けます。スケッチ内の特定機器向け解釈は、全機種共通のreport配置ではありません。

### APIの使い方と改造時の注意

HID reportは構造化された状態や変化であり、任意のテキスト一行ではありません。report descriptorと合わせて、report ID・usage page/usage・符号・数値範囲を読みます。キーボードはキー位置、マウス移動は相対値、ボタンはbitmapです。実際の操作へつなぐ前に、押下と解放の対、または全状態の比較を確認します。

callbackのreportは借りたデータです。別taskへ渡す場合はbyte列や必要な値を自分の領域へコピーします。callbackでは短い診断に留め、生dumpの連続表示で端末速度が律速になることにも注意します。入力が来なければ、列挙、descriptor・report長、アプリ側の解釈の順に調べ、一度に全部を変えないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostGamepad.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
