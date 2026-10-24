# EspUsbHostKeyboardDump

> English: [README.md](README.md)

キーボードの全イベントをシリアルモニターにダンプするサンプルです。押下・解放の区別、HIDキーコード、ASCII値、アクティブなモディファイアキーを表示します。

カスタムキーボードのデバッグ、キーコードマッピングの確認、ライブラリが生成するデータを文字解釈の前段階で確認するのに使います。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB HIDキーボード

## 動作内容

- キーの押下・解放ごとに`EspUsbHostKeyboardEvent`の全フィールドを表示
- モディファイアビットを名前付きフラグ（LSHIFT・RCTRLなど）にデコード
- レイアウト設定なし — keycode と ascii は生のHIDレポートをそのまま反映

## 主要API

- `usb.onKeyboard(callback)` — キー押下・解放時に`EspUsbHostKeyboardEvent`付きで呼ばれる
- `event.keycode` / `event.ascii` / `event.modifiers` — このサンプルの自前コールバックで使っているパース済みキーボード情報
- `event.rawData` / `event.rawLength` と `event.reportData` / `event.reportLength` — パース済みキーボード情報とあわせて元のHIDレポートバイトを参照可能

## シリアル出力例

```
connected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
[press  ] keycode=0x04 ascii=0x61(a) modifiers=none
[release] keycode=0x04 ascii=0x61(a) modifiers=none
[press  ] keycode=0x04 ascii=0x41(A) modifiers=LSHIFT
[release] keycode=0x04 ascii=0x41(A) modifiers=LSHIFT
[press  ] keycode=0x39 ascii=0x00(.) modifiers=none
[release] keycode=0x39 ascii=0x00(.) modifiers=none
```

## EspUsbHostHIDRawDumpとの違い

`EspUsbHostHIDRawDump`はUSB転送の生バイトをパース前に表示します。
このサンプルはライブラリのパース結果を表示します — キーコード解決とASCII変換後のイベントです。

## 関連サンプル

- [EspUsbHostKeyboard](../EspUsbHostKeyboard/) — 文字出力とLED連動
- [EspUsbHostHIDRawDump](../EspUsbHostHIDRawDump/) — 生HIDレポートバイト

## 読み方と確認の手順

### 実行前の準備

キーボードのusage・修飾キー・変換後の文字を調べる。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`onKeyboard()`で押下・解放の両方を表示します。文字単独、Shiftとの組合せ、文字にならないキーを比べます。usageはキーの識別、modifiersと配列は解釈に関わる値です。配列を変える前の確認に使います。

### APIの使い方と改造時の注意

HID reportは構造化された状態や変化であり、任意のテキスト一行ではありません。report descriptorと合わせて、report ID・usage page/usage・符号・数値範囲を読みます。キーボードはキー位置、マウス移動は相対値、ボタンはbitmapです。実際の操作へつなぐ前に、押下と解放の対、または全状態の比較を確認します。

callbackのreportは借りたデータです。別taskへ渡す場合はbyte列や必要な値を自分の領域へコピーします。callbackでは短い診断に留め、生dumpの連続表示で端末速度が律速になることにも注意します。入力が来なければ、列挙、descriptor・report長、アプリ側の解釈の順に調べ、一度に全部を変えないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostKeyboardDump.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
