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

- `usb.onVendorInput(callback)` — ベンダーHID入力レポートごとに`EspUsbHostVendorInput`付きで呼ばれる
  - `input.interfaceNumber` — HIDインターフェース番号
  - `input.length` — レポートのバイト長
  - `input.data` — レポートのデータ
- `usb.sendVendorOutput(data, length)` — HID出力レポートを送信
- `usb.sendVendorFeature(data, length)` — HIDフィーチャーレポートを送信

## シリアル出力例

```
connected: vid=303a pid=4004 product=ESP32S3 HID Vendor
vendor iface=0 len=63 data=device input
send output: ok
send feature: ok
```
