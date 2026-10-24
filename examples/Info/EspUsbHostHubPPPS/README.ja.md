# EspUsbHostHubPPPS

> English: [README.md](README.md)

ポート単位電源制御（PPPS: Per-Port Power Switching）に対応したUSBハブのポート電源をON/OFFするサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- ポート単位電源制御に対応したUSBハブ
- ハブのdownstreamポートに接続する任意のUSBデバイス

このサンプルではUSB 2.0対応のUSBハブを推奨します。ポート電源の挙動を安定させるにはセルフパワーのハブが望ましいですが、PPPS対応で安定して入手しやすい製品は多くありません。

## 動作内容

- USBハブの電源制御機能とポート状態を表示
- シリアルから操作対象のdownstreamポートを選択
- `usb.setHubPortPower()`で選択ポートの電源をOFF/ON
- 接続/切断コールバックを表示し、電源制御の結果を確認

## PPPSとギャングモード

USBハブの電源スイッチ方式はHub descriptorの`wHubCharacteristics`で報告されます。このサンプルの出力では、`ppps`、`ganged_power`、`no_power_switch`として表示します。

- `ppps=1` はPPPS（Per-Port Power Switching）です。ポートごとに電源をON/OFFできることを示します。このサンプルの主対象です。
- `ganged_power=1` はギャングモードです。ハブ全体、または複数ポートの電源がまとめて制御されます。ポート番号を指定しても、そのポートだけが切れるとは限りません。
- `no_power_switch=1` はソフトウェアからのポート電源制御がない、または報告されていない状態です。

ギャングモードのハブに対して`usb.setHubPortPower(hubAddress, port, false)`を呼ぶと、ハブ実装によっては全ポートがOFFになる、要求が失敗する、または状態表示だけが変わる場合があります。そのため、このサンプルはギャングモードの動作確認にも使えますが、「選択ポートだけを安全にOFFにする」用途にはPPPS対応ハブを使ってください。

## シリアルコマンド

| コマンド | 動作 |
|----------|------|
| `r` | ハブ情報と全ポート状態を再表示 |
| `1`-`9` | 操作対象ポートを選択 |
| `o` | 選択ポートの電源をOFF |
| `n` | 選択ポートの電源をON |

## 注意

- 実際にポート単位で制御するには、ハブがPPPS対応として報告される必要があります。
- USB 3.xハブでは内部トポロジーが異なる場合があるため、この機能の確認はUSB 2.0ハブから始めるのがおすすめです。
- ganged powerのハブでは、ポート電源コマンドが全ポートに影響するか、失敗する場合があります。
- ポート電源をOFFにすると、そのポート上のデバイスは切断されます。

## 主要API

- `usb.getHubInfo(hubAddress, hub)` — Hub descriptorとデコード済みハブ機能を取得
- `usb.getHubPortStatus(hubAddress, port, status, change)` — Hub port status/changeビットを取得
- `usb.setHubPortPower(hubAddress, port, enable)` — Hub port電源をON/OFF

## シリアル出力例

```
EspUsbHostHubPPPS start
Connect a USB hub that supports per-port power switching.
Commands:
  r  refresh hub and port status
  1-9  select port
  o  power off selected port
  n  power on selected port
CONNECTED address=1 portId=0x01 hub=1 vid=05e3 pid=0608
hub address=1 ports=4 ppps=1 ganged_power=0 no_power_switch=0
  port 1: status=0x0100 change=0x0000 powered=1 connected=0 enabled=0
  port 2: status=0x0303 change=0x0000 powered=1 connected=1 enabled=1
  port 3: status=0x0100 change=0x0000 powered=1 connected=0 enabled=0
  port 4: status=0x0100 change=0x0000 powered=1 connected=0 enabled=0
```

ポート2を`2`で選択し、`o`で電源をOFFにした場合：

```
selected port=2
  port 2: status=0x0303 change=0x0000 powered=1 connected=1 enabled=1
hub=1 port=2 power=off
request: OK
  port 2: status=0x0100 change=0x0001 powered=1 connected=0 enabled=0
DISCONNECTED address=2 portId=0x02 vid=045e pid=07a5
```

## 読み方と確認の手順

### 実行前の準備

hub各portの給電状態を調べ、個別の切り替えを要求する。

上のハードウェア・依存libraryの条件を確認し、boardと例のprofileを選び、診断Serialの接続先を確認します。[サンプルの進め方](../../../docs/example-workflow.ja.md)では導入・コネクタ・USBの役割・開始成功と相手の利用可能状態の違いを説明しています。

### コードを追う

最初にhubの能力を表示し、実際のhub addressと下流portを選びます。rで状態、o/nでoff/onを要求します。要求成功だけではVBUSが物理的に独立して切り替わった証拠にはならないので、hubの仕様と相手の再接続も確認します。

### APIの使い方と改造時の注意

hub addressはhub本体、port番号はその下流コネクタを示します。要求前に接続構造と提示された給電切替能力を調べます。softwareがportを指定できても、物理switchを共有するhubがあります。

給電の切断は相手の実行中の転送も中断します。storage・audioで試す前にはファイル書き込みやstreamを終えます。給電を戻すと新しいaddressが割り当たる場合があるため、古いhandleを使わず現在の接続eventから取り直します。

### 一つずつ確認する

最初は例を変更せず、上の操作とログ・ホストアプリの結果を照合します。相手がいない場合から接続へ進み、最終結果だけでなく、どの段階まで成功したかを見ます。端子・format・命令を一つ変える前に元の設定へ戻せるようにします。例のエラー表示だけで、汎用の再試行・回復方針まで実装されるわけではありません。

- [スケッチ](EspUsbHostHubPPPS.ino)
- [全サンプル](../../README.ja.md)
- [サンプルの進め方](../../../docs/example-workflow.ja.md)
