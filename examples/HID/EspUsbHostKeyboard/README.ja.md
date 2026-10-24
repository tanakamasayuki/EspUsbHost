# EspUsbHostKeyboard

> English: [README.md](README.md)

USBキーボードからHID入力を受け取り、入力した文字をシリアルモニターに表示するとともに、CapsLock・NumLock・ScrollLock LEDをキー操作に連動して制御するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB HIDキーボード

## 動作内容

- 日本語キーボードレイアウト（`ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP`）を設定
- 接続・切断時にデバイスのVID・PID・製品名を表示
- キーボードで入力した印字可能なASCII文字をシリアル出力する（Enterで改行）
- CapsLock・NumLock・ScrollLockの状態はライブラリが自動管理し、LEDへの反映と`event.ascii`への適用を行う

## 主要API

- `usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP)`
- `usb.onKeyboard(callback)` — キー押下・解放時に`EspUsbHostKeyboardEvent`付きで呼ばれる
  - `event.ascii` — CapsLock・NumLock状態を反映した文字
  - `event.capsLock` / `event.numLock` / `event.scrollLock` — このキー押下後の現在のロック状態
  - `event.rawData`, `event.rawLength` — 生のHID入力レポートバイト
  - `event.reportData`, `event.reportLength` — Report IDがある場合はそれを除いたキーボードレポートバイト
- `usb.getKeyboardCapsLock()` / `usb.getKeyboardNumLock()` / `usb.getKeyboardScrollLock()` — いつでもロック状態を読み出し可能
- `usb.setKeyboardLeds(numLock, capsLock, scrollLock)` — ロック状態とLEDを手動でオーバーライド
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`

## シリアル出力例

```
connected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
hello world
disconnected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
```

## 関連サンプル

- [EspUsbHostKeyboardDump](../EspUsbHostKeyboardDump/) — キーコード・モディファイア・ASCIIを全イベントでダンプ

## 読み方と確認の手順

### 実行前の準備

USBキーボードの文字入力とLock LEDの状態を読む。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`usb.begin()`より前にonKeyboard()を登録します。callbackは解放を除き、表示できるevent.asciiを出力します。文字・Enter・Caps Lockを試し、文字とLEDの変化を比較します。文字が出ないこととHID reportが来ないことは別です。

### APIの使い方と改造時の注意

HID reportは構造化された状態や変化であり、任意のテキスト一行ではありません。report descriptorと合わせて、report ID・usage page/usage・符号・数値範囲を読みます。キーボードはキー位置、マウス移動は相対値、ボタンはbitmapです。実際の操作へつなぐ前に、押下と解放の対、または全状態の比較を確認します。

callbackのreportは借りたデータです。別taskへ渡す場合はbyte列や必要な値を自分の領域へコピーします。callbackでは短い診断に留め、生dumpの連続表示で端末速度が律速になることにも注意します。入力が来なければ、列挙、descriptor・report長、アプリ側の解釈の順に調べ、一度に全部を変えないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostKeyboard.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
