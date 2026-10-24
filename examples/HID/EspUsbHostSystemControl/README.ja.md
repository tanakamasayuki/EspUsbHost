# EspUsbHostSystemControl

> English: [README.md](README.md)

USBキーボードからHIDシステムコントロールイベント（電源・スタンバイ・ウェイクアップ）を受け取り、シリアルモニターに表示するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- システムコントロールキー（電源ボタンなど）付きUSBキーボード

## 動作内容

- HIDシステムコントロールレポートを受信
- 押下・解放ごとにHIDユーセージコードと名称を表示

対応ユーセージコード（`EspUsbHost.h`で定義）：

| 定数 | 値 | 名称 |
|------|----|------|
| `ESP_USB_HOST_SYSTEM_CONTROL_POWER_OFF` | `0x01` | 電源オフ |
| `ESP_USB_HOST_SYSTEM_CONTROL_STANDBY` | `0x02` | スタンバイ |
| `ESP_USB_HOST_SYSTEM_CONTROL_WAKE_HOST` | `0x03` | ウェイクアップ |

`event.usage`はシステムコントロールレポートの先頭バイトを変換せずにそのまま渡した値なので、あるキーがどの値になるかはキーボードのレポートディスクリプタに依存します。論理値が1から始まる配列項目としてエンコードする機種では上表の`0x01`〜`0x03`になります。Generic Desktopのusage IDをそのまま送る機種では`0x81`（System Power Down）・`0x82`（System Sleep）・`0x83`（System Wake Up）が届き、この場合`espUsbHostSystemControlUsageName()`は空文字を返します。どちらのエンコードかは、お使いのデバイスで表示される`usage=0x..`の値で確認してください。

## 主要API

- `usb.onSystemControl(callback)` — 押下・解放時に`EspUsbHostSystemControlEvent`付きで呼ばれる
  - `event.pressed` — 押下でtrue、解放でfalse
  - `event.usage` — HIDユーセージコード（8ビット）
  - `event.rawData`, `event.rawLength` — 生のHID入力レポートバイト
  - `event.reportData`, `event.reportLength` — Report IDがある場合はそれを除いたシステムコントロールレポートバイト
- `espUsbHostSystemControlUsageName(event.usage)` — 代表的なSystem Control usageの読みやすい名前を返す

## シリアル出力例

```
system press usage=0x01 Power Off
system release usage=0x01 Power Off
```

## 読み方と確認の手順

### 実行前の準備

電源操作等のSystem Control HID入力を受け取る。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`onSystemControl()`でusageと押下・解放を区別します。相手が実際に備えるキーで確認し、普通の文字キーではこのcallbackに来ないことも確かめます。reportを観察する例であり、別のPCを終了させる処理ではありません。

### APIの使い方と改造時の注意

HID reportは構造化された状態や変化であり、任意のテキスト一行ではありません。report descriptorと合わせて、report ID・usage page/usage・符号・数値範囲を読みます。キーボードはキー位置、マウス移動は相対値、ボタンはbitmapです。実際の操作へつなぐ前に、押下と解放の対、または全状態の比較を確認します。

callbackのreportは借りたデータです。別taskへ渡す場合はbyte列や必要な値を自分の領域へコピーします。callbackでは短い診断に留め、生dumpの連続表示で端末速度が律速になることにも注意します。入力が来なければ、列挙、descriptor・report長、アプリ側の解釈の順に調べ、一度に全部を変えないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostSystemControl.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
