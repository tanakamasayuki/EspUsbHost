# EspUsbHostMouse

> English: [README.md](README.md)

USB HIDマウスの移動量・ボタン操作を受け取り、累積座標の追跡とクリック検出を行うサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- USB HIDマウス

## 動作内容

- 移動デルタを `posX` / `posY` に累積し、常に絶対座標として参照できる
- ボタンごとの押下・解放を立ち上がり／立ち下がりエッジで検出
- 移動イベントごとに累積座標とデルタを表示

## 主要API

- `usb.onMouse(callback)` — マウスイベントごとに`EspUsbHostMouseEvent`付きで呼ばれる
  - `event.buttonsChanged` — ボタン状態が変化したときtrue
  - `event.moved` — 位置が変化したときtrue
  - `event.x`, `event.y`, `event.wheel` — 相対移動量（`int16_t`）
  - `event.buttons`, `event.previousButtons` — ボタンのビットマスク（`uint8_t`）
  - `event.rawData`, `event.rawLength` — 生のHID入力レポートバイト
  - `event.reportData`, `event.reportLength` — Report IDがある場合はそれを除いたマウスレポートバイト

## シリアル出力例

```
connected: device: address=1 portId=0x01 vid=046d pid=c52b class=0x00(Device) speed=full product="USB Receiver"
left   press
pos: x=3 y=-2  delta: dx=3 dy=-2 wheel=0
pos: x=3 y=-3  delta: dx=0 dy=-1 wheel=0
left   release
pos: x=3 y=-3  delta: dx=0 dy=0 wheel=-1
```

## 読み方と確認の手順

### 実行前の準備

マウスの移動と各ボタンの押下・解放を読み解く。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

`onMouse()`は相対移動とボタンのbitmapを渡します。移動・停止・wheel・clickを別々に試します。posX/posYは差分を加算した値で、OSのポインタ座標ではありません。毎reportを新しいclickとせず、現在と変化したbitを比べます。

### APIの使い方と改造時の注意

HID reportは構造化された状態や変化であり、任意のテキスト一行ではありません。report descriptorと合わせて、report ID・usage page/usage・符号・数値範囲を読みます。キーボードはキー位置、マウス移動は相対値、ボタンはbitmapです。実際の操作へつなぐ前に、押下と解放の対、または全状態の比較を確認します。

callbackのreportは借りたデータです。別taskへ渡す場合はbyte列や必要な値を自分の領域へコピーします。callbackでは短い診断に留め、生dumpの連続表示で端末速度が律速になることにも注意します。入力が来なければ、列挙、descriptor・report長、アプリ側の解釈の順に調べ、一度に全部を変えないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostMouse.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
