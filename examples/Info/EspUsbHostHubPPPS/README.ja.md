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
