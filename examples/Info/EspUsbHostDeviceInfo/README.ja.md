# EspUsbHostDeviceInfo

> English: [README.md](README.md)

接続されたUSBデバイスのディスクリプタ情報（デバイス・インターフェース・エンドポイント）を詳細に表示するサンプルです。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- 任意のUSBデバイス

## 動作内容

- デバイス接続時に自動でディスクリプタ情報を全表示
- 切断時にアドレス・VID・PIDを表示
- シリアルから`r`を送ると、現在接続中の全デバイス情報を再表示

## シリアルコマンド

| コマンド | 動作 |
|----------|------|
| `r` | 接続中の全デバイス情報を再表示 |

## 表示される情報

**デバイスレベル:**
- USBアドレス・親アドレス/ポート（ハブ接続時）・バス速度
- VID:PID・デバイスクラス/サブクラス/プロトコル
- USBバージョン・デバイスバージョン・EP0最大パケットサイズ
- メーカー名・製品名・シリアル番号文字列
- コンフィギュレーション値・インターフェース数・総長・属性・最大電力

**インターフェースレベル:**
- インターフェース番号・代替設定・クラス/サブクラス/プロトコル・エンドポイント数

**エンドポイントレベル:**
- インターフェース番号・エンドポイントアドレス・方向（IN/OUT）・転送タイプ・最大パケットサイズ・インターバル

## 主要API

- `usb.getDevices(devices, maxDevices)` — 接続中の全デバイスを列挙
- `usb.getDevice(address, device)` — 指定デバイスのディスクリプタを取得
- `usb.getInterfaces(address, interfaces, maxInterfaces)` — インターフェース一覧を取得
- `usb.getEndpoints(address, endpoints, maxEndpoints)` — エンドポイント一覧を取得
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`

## シリアル出力例

```
EspUsbHostDeviceInfo start
Press 'r' to reprint connected devices.
CONNECTED address=1

=========== USB Device ===========
Address 1 parent=root speed=full-speed
VID:PID 045e:07a5 class=0x00(per-interface) subclass=0x00 protocol=0x00
USB 2.00 device 0.01 ep0=8
Strings manufacturer="Microsoft" product="USB Keyboard" serial=""
Configuration value=1 interfaces=2 total_len=59 attributes=0xa0 max_power=100mA
  Interface 0 alt=0 class=0x03(HID) subclass=0x01 protocol=0x01 endpoints=1
  Interface 1 alt=0 class=0x03(HID) subclass=0x00 protocol=0x00 endpoints=1
    Endpoint iface=0 ep=0x81 dir=IN type=interrupt max_packet=8 interval=10 attrs=0x03
    Endpoint iface=1 ep=0x82 dir=IN type=interrupt max_packet=8 interval=10 attrs=0x03
========= USB Device End =========
```
