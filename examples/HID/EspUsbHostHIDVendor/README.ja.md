# EspUsbHostHIDVendor

> English: [README.md](README.md)

ベンダークラスHID通信のサンプルです。USB HIDベンダーデバイスからの入力レポートを受信し、出力レポートおよびフィーチャーレポートを送信します。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB HIDベンダーデバイス（ベンダーHIDペリフェラルスケッチを実行するESP32-S3を想定）

## 動作内容

- ベンダーHID入力レポートを受信してシリアルに内容を表示
- シリアルコマンドに応じて固定63バイトの出力レポートまたはフィーチャーレポートを送信

## シリアルコマンド

| コマンド | 動作 |
|----------|------|
| `o` | 出力レポートを送信（`"host output"`） |
| `f` | フィーチャーレポートを送信（`"host feature"`） |

## 主要API

- `usb.onHIDVendorInput(callback)` — ベンダーHID入力レポートごとに`EspUsbHostHIDVendorInput`付きで呼ばれる
  - `input.interfaceNumber` — HIDインターフェース番号
  - `input.rawData`, `input.rawLength` — 生のHID入力レポートバイト
  - `input.reportData`, `input.reportLength` — Report IDがある場合はそれを除いたレポートバイト
- `usb.sendHIDVendorOutput(data, length)` — interrupt OUT endpointへ渡されたバイト列をそのまま送信。Report ID付きのdescriptorなら `[Report ID, payload…]` を渡します。IDなしならpayloadだけを渡します。IDの自動挿入・除去はしません。IDを別引数で指定するAPIから移植するときは、この違いに注意してください。
- `usb.sendHIDVendorFeature(data, length)` — EP0の`SET_REPORT`でHIDフィーチャーレポートを送信

## シリアル出力例

```
connected: device: address=1 portId=0x01 vid=303a pid=4004 class=0x00(Device) speed=full product="ESP32S3 HID Vendor"
vendor iface=0 len=63 data=device input
send output: ok
send feature: ok
```

## 読み方と確認の手順

### 実行前の準備

Vendor HID入力を受け、対応するOutput・Feature reportを送る。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

UARTのo・fで用意したOutput・Featureのデータを送り、`onHIDVendorInput()`で応答を見ます。同じdescriptorと手順を実装した相手を使い、report IDとpayload長を合わせます。Vendor HIDとVendor bulkは別の通信です。

### APIの使い方と改造時の注意

HID reportは構造化された状態や変化であり、任意のテキスト一行ではありません。report descriptorと合わせて、report ID・usage page/usage・符号・数値範囲を読みます。キーボードはキー位置、マウス移動は相対値、ボタンはbitmapです。実際の操作へつなぐ前に、押下と解放の対、または全状態の比較を確認します。

callbackのreportは借りたデータです。別taskへ渡す場合はbyte列や必要な値を自分の領域へコピーします。callbackでは短い診断に留め、生dumpの連続表示で端末速度が律速になることにも注意します。入力が来なければ、列挙、descriptor・report長、アプリ側の解釈の順に調べ、一度に全部を変えないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostHIDVendor.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
