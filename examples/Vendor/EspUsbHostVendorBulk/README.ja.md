# EspUsbHostVendorBulk

> English: [README.md](README.md)

汎用（非HID）vendor-specificインターフェースAPIのサンプルです。`bInterfaceClass == 0xff` のインターフェースをclaimし、bulk IN/OUT転送とEP0 vendor control IN/OUTリクエストを行います。

## ハードウェア

- ESP32-S3（またはArduino-ESP32 USB Hostに対応したボード）
- bulk IN/OUTエンドポイントを持つvendor-specific USBデバイス — 例として `tests/peer/usb_vendor` のpeerスケッチ（`EspUsbDeviceVendor`）を実行するESP32-S3

## 動作内容

- 接続時にvendor-specificインターフェースをclaim（`vendorOpen`）し、bulk IN受信を開始
- 受信したbulk INペイロードを `onVendorData` で表示
- シリアルコマンドでbulk OUT送信・受信バッファ読み出し・EP0 vendor controlリクエストを実行

`tests/peer/usb_vendor` のpeerは、bulk OUT `"ping"` を `"echo:ping"` としてエコーバックし、control IN `bRequest=0x01` で自身の名前を返し、control OUT `bRequest=0x02` を受け付けます。

## シリアルコマンド

| コマンド | 動作 |
|----------|------|
| `w` | bulk OUT `"ping"`（peerがbulk INで `"echo:ping"` を返す） |
| `r` | デバイスごとの受信バッファからノンブロッキング読み出し |
| `c` | EP0 vendor control IN、`bRequest=0x01` |
| `o` | EP0 vendor control OUT、`bRequest=0x02` |

## 主要API

- `usb.vendorOpen(address)` — vendor-specificインターフェースを明示的にclaimし、bulk IN受信を開始
- `usb.onVendorData(callback)` — bulk INペイロードごとに `EspUsbHostVendorData` 付きで呼ばれる。`data`ポインタはコールバック中のみ有効
- `usb.vendorWrite(data, length, address)` — bulk OUT転送
- `usb.vendorRead(buffer, length, address)` — 512バイトのデバイスごと受信バッファからのノンブロッキング読み出し
- `usb.vendorControlIn(request, value, index, data, length, &actual, address)` — EP0 vendor control IN（`bmRequestType = 0xc0`）
- `usb.vendorControlOut(request, value, index, data, length, address)` — EP0 vendor control OUT（`bmRequestType = 0x40`）

## シリアル出力例

```
EspUsbHost vendor bulk/control example start
connected: device: address=1 portId=0x01 vid=303a pid=4019 class=0x00(Device) speed=full product="EspUsbDevice USB Vendor"
vendorOpen: ok
bulk write: ok
vendor in iface=0 ep=0x81 len=9 data=echo:ping
bulk read: len=0 data=
control in: ok len=17 data=EspUsbDeviceVendor
control out: ok
```
