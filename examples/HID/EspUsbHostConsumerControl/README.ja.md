# EspUsbHostConsumerControl

> English: [README.md](README.md)

USBキーボードやリモコンからHIDコンシューマーコントロールイベント（メディアキー）を受け取り、シリアルモニターに表示するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- コンシューマーコントロールキー（音量・再生/一時停止など）付きUSBキーボードまたはリモコン

## 動作内容

- HIDコンシューマーコントロールレポートを受信
- キーの押下・解放ごとにHIDユーセージコードと、主要なコードの名称を表示

対応している主なユーセージコード：

| ユーセージ | 名称 |
|------------|------|
| `ESP_USB_HOST_CONSUMER_CONTROL_MUTE` | ミュート |
| `ESP_USB_HOST_CONSUMER_CONTROL_VOLUME_UP` | 音量アップ |
| `ESP_USB_HOST_CONSUMER_CONTROL_VOLUME_DOWN` | 音量ダウン |
| `ESP_USB_HOST_CONSUMER_CONTROL_PLAY_PAUSE` | 再生/一時停止 |
| `ESP_USB_HOST_CONSUMER_CONTROL_NEXT_TRACK` | 次のトラック |
| `ESP_USB_HOST_CONSUMER_CONTROL_PREVIOUS_TRACK` | 前のトラック |

## 主要API

- `usb.onConsumerControl(callback)` — 押下・解放時に`EspUsbHostConsumerControlEvent`付きで呼ばれる
  - `event.pressed` — 押下でtrue、解放でfalse
  - `event.usage` — HIDユーセージコード（16ビット）
  - `event.rawData`, `event.rawLength` — 生のHID入力レポートバイト
  - `event.reportData`, `event.reportLength` — Report IDがある場合はそれを除いたコンシューマーコントロールレポートバイト
- `espUsbHostConsumerControlUsageName(event.usage)` — 代表的なConsumer usageの読みやすい名前を返す

## シリアル出力例

```
connected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
consumer press usage=0x00e9 Volume Up
consumer release usage=0x00e9 Volume Up
consumer press usage=0x00cd Play/Pause
consumer release usage=0x00cd Play/Pause
```

## 読み方と確認の手順

### 実行前の準備

メディアキーの押下・解放を受け取る。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

音量や再生操作はonConsumerControl()で受け取ります。押す・離すでevent.pressedを比較します。通常のキーボードとは別のusage pageです。この例はESP32で入力を表示し、PCの音量を操作するものではありません。

### APIの使い方と改造時の注意

HID reportは構造化された状態や変化であり、任意のテキスト一行ではありません。report descriptorと合わせて、report ID・usage page/usage・符号・数値範囲を読みます。キーボードはキー位置、マウス移動は相対値、ボタンはbitmapです。実際の操作へつなぐ前に、押下と解放の対、または全状態の比較を確認します。

callbackのreportは借りたデータです。別taskへ渡す場合はbyte列や必要な値を自分の領域へコピーします。callbackでは短い診断に留め、生dumpの連続表示で端末速度が律速になることにも注意します。入力が来なければ、列挙、descriptor・report長、アプリ側の解釈の順に調べ、一度に全部を変えないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostConsumerControl.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
