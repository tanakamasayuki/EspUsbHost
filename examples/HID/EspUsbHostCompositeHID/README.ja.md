# EspUsbHostCompositeHID

> English: [README.md](README.md)

1台の複合USB HIDデバイス、またはUSBハブ経由で接続された複数のUSB HIDデバイスから、複数種類のHID入力を同時に読むサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- 1台の複合USB HIDデバイス、または複数のUSB HIDデバイス。例:
  - トラックパッド付きキーボード
  - メディアキー付きキーボード
  - キーボードとマウスのインターフェースを持つワイヤレスレシーバー
  - USBハブ経由で接続した個別のUSBキーボードとUSBマウス

## 動作内容

- 1つの `EspUsbHost` インスタンスに複数のパース済みHIDコールバックを登録
- キーボードのキー押下を、デバイスアドレスとインターフェース番号付きで表示
- マウス／トラックパッドの移動、ボタン変化、ホイール移動を表示
- 音量や再生キーなどのConsumer Control usageを表示
- Power、Standby、WakeなどのSystem Control usageを表示
- `event.address` と `event.interfaceNumber` で、どのデバイス／インターフェースからのイベントか確認

このサンプルは入力レポートに絞っています。キーボードLEDなどの出力レポートは、デバイスごとのReport IDや出力レポート構造に依存しやすいため含めていません。

## 主要API

- `usb.onKeyboard(callback)` — キーボードのキー押下・解放イベントで呼ばれる
- `usb.onMouse(callback)` — マウスまたはトラックパッドのイベントで呼ばれる
- `usb.onConsumerControl(callback)` — メディアキーなどのConsumer Page操作で呼ばれる
- `usb.onSystemControl(callback)` — System Control usageで呼ばれる
- `event.address` / `event.interfaceNumber` — どのデバイス／インターフェースからイベントが来たか確認するときに便利
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`

## シリアル出力例

```
EspUsbHost multiple HID example start
connected: device: address=1 portId=0x01 vid=05ac pid=0262 class=0x00(Device) speed=full product="Keyboard with Trackpad"
keyboard: address=1 iface=0 keycode=0x04 ascii='a'
mouse: address=1 iface=1 pos: x=3 y=-2 delta: dx=3 dy=-2 wheel=0
consumer: address=1 iface=2 press usage=0x00e9 Volume Up
consumer: address=1 iface=2 release usage=0x00e9 Volume Up
system: address=1 iface=3 press usage=0x02 Standby
```

システムコントロールの`usage`バイトはデバイス依存で、ここに示した`0x01`〜`0x03`ではなくGeneric Desktopのusage ID `0x81`〜`0x83`を送るキーボードもあります。詳細は [EspUsbHostSystemControl](../EspUsbHostSystemControl/) を参照してください。

## 関連

- [EspUsbHostKeyboard](../EspUsbHostKeyboard/) — 基本的なキーボード文字入力
- [EspUsbHostMouse](../EspUsbHostMouse/) — 基本的なマウス移動・ボタン処理
- [EspUsbHostConsumerControl](../EspUsbHostConsumerControl/) — メディアキー処理
- [EspUsbHostSystemControl](../EspUsbHostSystemControl/) — 電源／スリープ／復帰処理
- [EspUsbHostHIDRawDump](../EspUsbHostHIDRawDump/) — トラブルシュート用の生HIDレポート表示

## 読み方と確認の手順

### 実行前の準備

複合HID機器のキーボード・マウス・メディア入力を読む。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

同じusbへ種類別のHID callbackを登録します。一機能ずつ操作し、event.addressと各イベントを比較します。一つの物理機器が複数reportを出すことと、複数の別機器があることは区別します。

### APIの使い方と改造時の注意

HID reportは構造化された状態や変化であり、任意のテキスト一行ではありません。report descriptorと合わせて、report ID・usage page/usage・符号・数値範囲を読みます。キーボードはキー位置、マウス移動は相対値、ボタンはbitmapです。実際の操作へつなぐ前に、押下と解放の対、または全状態の比較を確認します。

callbackのreportは借りたデータです。別taskへ渡す場合はbyte列や必要な値を自分の領域へコピーします。callbackでは短い診断に留め、生dumpの連続表示で端末速度が律速になることにも注意します。入力が来なければ、列挙、descriptor・report長、アプリ側の解釈の順に調べ、一度に全部を変えないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostCompositeHID.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
