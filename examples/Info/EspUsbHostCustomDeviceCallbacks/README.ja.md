# EspUsbHostCustomDeviceCallbacks

> English: [README.md](README.md)

共通の`espUsbHostPrint(device)`サマリ表示に、自分の接続・切断処理を足す例です。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- 任意のUSBデバイス（キーボード、マウス、ハブなど）

## 動作内容

- USBデバイス接続・切断時に共通形式の1行サマリーを表示
- 接続デバイスのインターフェースを調べ、キーボードらしいインターフェースがあるか表示
- 接続デバイスがUSBハブかどうかを表示
- 現在の接続デバイス数を表示
- シリアルから`d`を送ると詳細ディスクリプタ情報を全表示

## シリアルコマンド

| コマンド | 動作 |
|----------|------|
| `d` | 接続中の全デバイスの詳細ディスクリプタ情報を表示 |

## 主要API

- `usb.onDeviceConnected(onDeviceConnected)` — 名前付きの接続コールバック関数を登録
- `usb.onDeviceDisconnected(onDeviceDisconnected)` — 名前付きの切断コールバック関数を登録
- `espUsbHostPrint(device)` — 共通形式の1行デバイスサマリーを表示
- `usb.getInterfaces(address, interfaces, maxInterfaces)` — コールバック内でインターフェース情報を調べる
- `device.isHub` — 接続デバイスがUSBハブのときtrue
- `usb.deviceCount()` — 現在接続されているデバイス数を取得
- `usb.printAllDeviceInfo()` — 必要なときだけ詳細ディスクリプタ情報を表示

## シリアル出力例

```
EspUsbHost custom device callbacks example start
Press 'd' to print the full descriptor dump.
connected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
  this device has a keyboard interface
  total devices: 1
disconnected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
  total devices: 0
```

## 読み方と確認の手順

### 実行前の準備

interface情報を使って接続機器を分類する。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`isKeyboardLike()`は機器全体のclassだけでなくinterfaceを調べます。キーボードと複合機器を順に試し、dで全体を表示します。接続callbackを登録するだけでは、新しいUSB class driverが追加されるわけではありません。

### APIの使い方と改造時の注意

内部開始、列挙、interfaceの発見、classの実通信を別の確認点にします。descriptorは相手が提示した内容であり、実転送やprotocolの実装を保証しません。descriptorの走査を改造するときも受信長と容量検査を残します。interface番号・endpoint address・USB address・物理portは別の識別子です。

最初は同じ機器の直接接続と再接続を比べ、理解してからhubや機能を増やします。異常ではエラー名と失敗した操作を一緒に控えます。大量dumpは診断用であり、高頻度callbackの処理例としてそのまま使わないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostCustomDeviceCallbacks.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
