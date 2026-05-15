# EspUsbHostDeviceInfo

> English: [README.md](README.md)

接続されたUSBデバイスの情報を詳細に表示するサンプルです。未対応デバイスも調査用に表示し、USBハブの機能・ポート状態も取得できる範囲で表示します。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- 任意のUSBデバイス
- 任意: USBハブ（ポート単位の電源制御に対応したものだと情報を確認しやすい）

## 動作内容

- デバイス接続時に自動でディスクリプタ情報を全表示
- 切断時にアドレス・VID・PIDを表示
- 列挙が落ち着いた後に全体トポロジーを自動表示
- シリアルから`r`を送ると、現在接続中の全デバイス情報とハブ状態を再表示
- 未対応デバイスもVID/PIDやdescriptor情報を表示し、無反応にしない
- USBハブのdescriptor、PPPS、ポート状態を表示

## シリアルコマンド

| コマンド | 動作 |
|----------|------|
| `r` | 接続中の全デバイス情報とハブ状態を再表示 |

## 表示される情報

**デバイスレベル:**
- USBアドレス・ポートID・親アドレス（ハブ接続時）・バス速度
- VID:PID・デバイスクラス/サブクラス/プロトコル
- ライブラリ対応状態（`supported=yes/no`）・ハブ判定
- USBバージョン・デバイスバージョン・EP0最大パケットサイズ
- メーカー名・製品名・シリアル番号文字列
- コンフィギュレーション値・インターフェース数・総長・属性・bus/self powered・remote wakeup・最大電力

**インターフェースレベル:**
- インターフェース番号・代替設定・クラス/サブクラス/プロトコル・エンドポイント数

**エンドポイントレベル:**
- インターフェース番号・エンドポイントアドレス・方向（IN/OUT）・転送タイプ・最大パケットサイズ・インターバル

**ハブレベル:**
- Hub descriptor長・raw descriptor
- downstreamポート数
- `wHubCharacteristics`
- ポート単位電源制御（PPPS）・ganged power・電源制御なし
- ポート単位過電流通知・ganged over-current・過電流通知なし
- compound deviceフラグ
- power-on-to-power-good時間・ハブコントローラ消費電流

**ハブポートレベル:**
- port status/changeビット
- connected・enabled・suspended・over-current・reset・powered・low-speed・high-speed・test・indicator

## 主要API

- `usb.getDevices(devices, maxDevices)` — 接続中の全デバイスを列挙
- `usb.getDevice(address, device)` — 指定デバイスのディスクリプタを取得
- `usb.getHubInfo(hubAddress, hub)` — Hub descriptorとデコード済みハブ機能を取得
- `usb.getHubPortStatus(hubAddress, port, status, change)` — Hub port status/changeビットを取得
- `usb.getInterfaces(address, interfaces, maxInterfaces)` — インターフェース一覧を取得
- `usb.getEndpoints(address, endpoints, maxEndpoints)` — エンドポイント一覧を取得
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`

## シリアル出力例

```
EspUsbHostDeviceInfo start
Press 'r' to reprint connected devices and hub status.
CONNECTED address=1

=========== USB Device ===========
Address 1 portId=0x01 parent=root speed=full-speed
VID:PID 045e:07a5 class=0x00(per-interface) subclass=0x00 protocol=0x00
Supported=yes hub=no
USB 2.00 device 0.01 ep0=8
Strings manufacturer="Microsoft" product="USB Keyboard" serial=""
Configuration value=1 interfaces=2 total_len=59 attributes=0xa0(bus-powered remote_wakeup=yes) max_power=100mA
  Interface 0 alt=0 class=0x03(HID) subclass=0x01 protocol=0x01 endpoints=1
  Interface 1 alt=0 class=0x03(HID) subclass=0x00 protocol=0x00 endpoints=1
    Endpoint iface=0 ep=0x81 dir=IN type=interrupt max_packet=8 interval=10 attrs=0x03
    Endpoint iface=1 ep=0x82 dir=IN type=interrupt max_packet=8 interval=10 attrs=0x03
========= USB Device End =========

=========== USB Topology ===========
----------- USB Hub -----------
Hub address=1 ports=4 descriptor_len=9 characteristics=0x00a9
Power switching: per-port=yes ganged=no none=no
Over-current: per-port=yes ganged=no none=no
Compound=no power_on_to_good=100ms controller_current=100mA
Raw hub descriptor: 09 29 04 a9 00 32 64 00 ff
  Port 1 status=0x0303 change=0x0000 connected=yes enabled=yes suspended=no over_current=no reset=no powered=yes low_speed=yes high_speed=no test=no indicator=no
--------- USB Hub End ---------
Tracked devices=1
========= USB Topology End =========
```
