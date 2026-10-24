# EspUsbHostHIDReportDescriptor

> English: [README.md](README.md)

接続されたHIDデバイスのHIDレポートディスクリプタを表示するサンプルです。

通常のアプリケーションでレポートディスクリプタを毎回表示する必要はあまりありません。新しいHIDデバイスの調査、Report IDの確認、製品ごとに軸やボタン配置が異なるゲームパッド対応を設計するときに役立ちます。

## ハードウェア

- ESP32-S3、またはArduino-ESP32 USB Hostに対応したボード
- キーボード、マウス、ゲームパッド、カスタムHIDなどのUSB HIDデバイス

## 動作内容

- 接続時に通常のUSBデバイス情報を表示
- `onHIDReportDescriptor()`でHIDレポートディスクリプタを非同期に取得
- raw descriptor bytesを表示
- HID itemの簡易デコードを表示

## 主要API

- `usb.onHIDReportDescriptor(callback)` — 列挙後にHIDレポートディスクリプタを受け取る
- `espUsbHostPrint(descriptor)` — メタデータ、raw bytes、簡易item decodeを表示
- `espUsbHostPrintHIDReportDescriptor(data, length)` — raw descriptor bufferだけを表示

## シリアル出力例

```
EspUsbHostHIDReportDescriptor start
Connect a HID device to print its HID report descriptor.
CONNECTED address=1 vid=303a pid=4004 product="HID Device"

===== HID Report Descriptor #1 =====
hid report descriptor: address=1 iface=0 hid=0x0111 country=0x00 type=0x22 reported_len=38 len=38
Raw HID report descriptor: 05 01 09 04 A1 01 ...
HID report descriptor: len=38
  0000: 0x05 Global   Usage Page         value=1 raw=01
  0002: 0x09 Local    Usage              value=4 raw=04
  0004: 0xa1 Main     Collection         value=1 raw=01
=== HID Report Descriptor End ===
```

## 読み方と確認の手順

### 実行前の準備

HID入力byteの解釈に使うreport descriptorを取得する。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`begin()`前にonHIDReportDescriptor()を登録し、HID機器をつなぎ直して取得を観察します。生入力のdumpとreport ID・field幅・usageを照合します。configuration descriptorとHID report descriptorは説明する層が異なります。

### APIの使い方と改造時の注意

内部開始、列挙、interfaceの発見、classの実通信を別の確認点にします。descriptorは相手が提示した内容であり、実転送やprotocolの実装を保証しません。descriptorの走査を改造するときも受信長と容量検査を残します。interface番号・endpoint address・USB address・物理portは別の識別子です。

最初は同じ機器の直接接続と再接続を比べ、理解してからhubや機能を増やします。異常ではエラー名と失敗した操作を一緒に控えます。大量dumpは診断用であり、高頻度callbackの処理例としてそのまま使わないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostHIDReportDescriptor.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
