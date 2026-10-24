# EspUsbHostBringUpCheck

> English: [README.md](README.md)

新しいボードを使うとき、あるいはデバイスが動かないときに、最初に動かすスケッチです。確認するのは3点だけです。ホストコントローラが起動したか、何かが列挙されたか、その速度は何か。何も列挙されない場合は、空のログを出す代わりに物理的な原因のチェックリストを表示します。

[docs/usb-host-guide.ja.md](../../../docs/usb-host-guide.ja.md) の手順の一部です。

## ハードウェア

- ESP32-S2 / ESP32-S3 / ESP32-P4
- 任意のUSBデバイス（最初の確認には普通のUSBキーボードかUSBメモリが最適）

## 動作内容

- チップ、arduino-esp32バージョン、ライブラリバージョン、選択したホストポート、エンドポイントチャネル数を表示
- `usb.begin()` の成否を表示し、ビルド／設定の問題と配線の問題を切り分ける
- 2秒ごとに、デバイス数・使用チャネル数・空きヒープのステータス行を表示
- 接続時に、速度・VID:PID・文字列・デバイスクラス・セルフ／バスパワー・`bMaxPower`・ライブラリ対応可否を表示
- 10秒間デバイスが現れない場合、VBUS／コネクタ／ケーブル／電源／デバイスのチェックリストを表示

## 設定

`USE_HIGH_SPEED_PORT`（スケッチ冒頭、ESP32-P4専用）で、フルスピードOTGポートの代わりにハイスピードOTGポートを選びます。他のターゲットでは無視されます。ESP32-S2 / ESP32-S3のホストはフルスピードのみです。

## 主要API

- `usb.begin(config)` / `usb.lastErrorName()`
- `EspUsbHostConfig::port` — ESP32-P4での `ESP_USB_HOST_PORT_FULL_SPEED` / `ESP_USB_HOST_PORT_HIGH_SPEED`
- `usb.deviceCount()`, `usb.endpointChannelCount()`, `usb.maxEndpointChannelCount()`
- `usb.onDeviceConnected()` / `usb.onDeviceDisconnected()`

## 期待されるシリアル出力

```
EspUsbHost bring-up check start

--- environment ---
chip           : ESP32-S3 rev 0
arduino-esp32  : 3.3.11
EspUsbHost     : 2.7.8
free heap      : 289012 bytes
host port      : full-speed OTG (this target has no high-speed host)
channel budget : 8 endpoint channels

usb.begin(): ok -- the host controller is running
Plug in a USB device now.
[    2s] devices=0 channels=0/8 heap=270112

ENUMERATED address=1 speed=full-speed (12Mbps)
  045e:07a5 "Microsoft" / "USB Keyboard"
  device class=0x00 subclass=0x00 protocol=0x00 interfaces=2
  library support=yes hub=no max_power=100mA (bus-powered)
  channels claimed=2/8
  Next: run examples/Info/EspUsbHostDeviceExplorer for the full layout.
[    4s] devices=1 channels=2/8 heap=262340
```

## 次のステップ

ここでデバイスが列挙できたら、[EspUsbHostDeviceExplorer](../EspUsbHostDeviceExplorer/) を実行して、インターフェース構成とそれを扱うAPIを確認します。

## 読み方と確認の手順

### 実行前の準備

Hostの開始と接続機器の列挙を分けて確認する。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

beginOkと定期表示の機器数を、classの解析より先に見ます。`usb.begin()`が成功してもバス上の機器が0のことはあります。列挙されなければ表示されたコネクタ・給電の確認へ進み、認識後に未対応interfaceを調べます。

### APIの使い方と改造時の注意

内部開始、列挙、interfaceの発見、classの実通信を別の確認点にします。descriptorは相手が提示した内容であり、実転送やprotocolの実装を保証しません。descriptorの走査を改造するときも受信長と容量検査を残します。interface番号・endpoint address・USB address・物理portは別の識別子です。

最初は同じ機器の直接接続と再接続を比べ、理解してからhubや機能を増やします。異常ではエラー名と失敗した操作を一緒に控えます。大量dumpは診断用であり、高頻度callbackの処理例としてそのまま使わないようにします。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostBringUpCheck.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
