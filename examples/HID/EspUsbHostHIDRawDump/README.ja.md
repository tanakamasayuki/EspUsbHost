# EspUsbHostHIDRawDump

> English: [README.md](README.md)

接続されたすべてのデバイスから、デバイスアドレス・インターフェース・プロトコル情報付きで生のHID入力レポートをダンプするサンプルです。
複数のUSBデバイスを同時に扱えます。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB HIDデバイス1台以上（キーボード・マウス・ゲームパッドなど）

## 動作内容

- 接続・切断時にデバイスアドレス・VID・PID・製品名を表示
- 生のHID入力レポートをスペース区切りのHexでダンプし、アドレスとインターフェース情報も付与

## 主要API

- `usb.onHIDInput(callback)` — HID入力レポートごとに`EspUsbHostHIDInput`付きで呼ばれる
  - `input.address` — USBデバイスアドレス（複数デバイスの区別に使用）
  - `input.vid`, `input.pid` — デバイス別処理に使えるUSBベンダーID/プロダクトID
  - `input.product` — 取得できた場合のUSBプロダクト文字列
  - `input.interfaceNumber` — HIDインターフェース番号
  - `input.subclass` — HIDサブクラス（0=なし、1=ブートインターフェース）
  - `input.protocol` — HIDプロトコル（0=なし、1=キーボード、2=マウス）
  - `input.length` — レポートのバイト長
  - `input.data` — 生のレポートバイト列
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`
  - `device.address` — USBデバイスアドレス
- `espUsbHostPrint(input)` / `espUsbHostPrint(device)` — このサンプルで使っている1行シリアルダンプ用ヘルパー

## シリアル出力例

```
connected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
hid: address=1 iface=0 subclass=0x01 protocol=0x01 len=8 data=00 00 00 00 00 00 00 00
hid: address=1 iface=0 subclass=0x01 protocol=0x01 len=8 data=00 00 04 00 00 00 00 00
disconnected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
```

## 関連サンプル

- [EspUsbHostDeviceInfo](../../Info/EspUsbHostDeviceInfo/) — デバイス・インターフェース・エンドポイントの詳細表示

## 読み方と確認の手順

### 実行前の準備

アプリ固有の解釈を加える前の生HID入力を表示する。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

専用callbackで扱えない機器はonHIDInput()から調べられます。一入力ずつ変え、位置・report ID・長さを比較します。次にdescriptor例と照合し、byteが変わっただけで符号や単位を決めないようにします。

### APIの使い方と改造時の注意

HID reportは構造化された状態や変化であり、任意のテキスト一行ではありません。report descriptorと合わせて、report ID・usage page/usage・符号・数値範囲を読みます。キーボードはキー位置、マウス移動は相対値、ボタンはbitmapです。実際の操作へつなぐ前に、押下と解放の対、または全状態の比較を確認します。

callbackのreportは借りたデータです。別taskへ渡す場合はbyte列や必要な値を自分の領域へコピーします。callbackでは短い診断に留め、生dumpの連続表示で端末速度が律速になることにも注意します。入力が来なければ、列挙、descriptor・report長、アプリ側の解釈の順に調べ、一度に全部を変えないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostHIDRawDump.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
