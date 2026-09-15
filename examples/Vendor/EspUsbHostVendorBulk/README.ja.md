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
| `q` | 非同期read queueを開始（8 KBのIN転送を2本同時に飛ばす） |
| `e` | read queueを停止し、カウンタを表示 |

## 主要API

- `usb.vendorOpen(address)` — vendor-specificインターフェースを明示的にclaimし、bulk IN受信を開始
- `usb.onVendorData(callback)` — bulk INペイロードごとに `EspUsbHostVendorData` 付きで呼ばれる。`data`ポインタはコールバック中のみ有効
- `usb.vendorWrite(data, length, address)` — bulk OUT転送
- `usb.vendorRead(buffer, length, address)` — 512バイトのデバイスごと受信バッファからのノンブロッキング読み出し
- `usb.vendorControlIn(request, value, index, data, length, &actual, address)` — EP0 vendor control IN（`bmRequestType = 0xc0`）
- `usb.vendorControlOut(request, value, index, data, length, address)` — EP0 vendor control OUT（`bmRequestType = 0x40`）
- `usb.vendorReadQueueBegin(depth, bufferBytes, address)` — 1パケットずつではなく、bulk IN転送を複数本出しっぱなしにする。流しっぱなしのデバイス向け。endpointを遊ばせていないかは `usb.vendorReadStats(address)` で分かり、停止は `usb.vendorReadQueueEnd(address)`。キューを使わず転送サイズだけ変えるなら `usb.vendorOpen(address, 0xff, ESP_USB_HOST_VENDOR_READ_CONTINUOUS, bytes)`

## ストリームの調整

`q` は `vendorReadQueueBegin(2, 8192)` でキューを開始します。8 KB の転送を 2 本同時に飛ばす形です。2 つの数字は役割が違います。

- **同時に飛ばす転送数（`depth`）** は完了から次の submit までの折り返しを覆います。`depth` 1 では、完了した転送を再 submit する間 endpoint が応答できる転送を持たず、`vendorReadStats().starved` が完了のたびに 1 増えます。2 にすればそれが止まります。
- **1 転送のバイト数（`bufferBytes`）** は、その折り返しを何回払うかを決めます。既定の「1 転送＝最大サイズの 1 パケット」は、この API で最も遅い形です。

**多くの場合、天井を決めるのは device 側であって、こちら側ではありません。** endpoint を遊ばせていない状態まで来ると、host は device が供給する以上には読めず、この 2 つの数字をさらに動かしても何も変わりません。どちらなのかは `vendorReadStats()` で分かれます。`starved` は完了時に他に 1 本も飛んでいなかった回数で、これはこちらが十分訊けていない場合です。`bytes / completed` は device が実際に 1 転送へ詰められた量で、これは device がそれ以上出せない場合です。

ESP32-P4 同士の high speed では、キューは「1 転送＝1 パケット」の既定の数倍の値になり、`depth` 1 は完了のたびに starve する一方 `depth` 2 では 1 度も starve せず、1 転送が数 KB を超えたあたりから結果が動かなくなりました。どこで止まるかを決めていたのはこちら側ではなく peer 側で、host 側のコードを変えないまま peer が自身の `write()` に 1 回で渡す量を大きくすると、掃引全体が上がりました。ストリームが思ったより遅く、しかも `starved` がすでに 0 なら、次に変えるべきはここではなく device 側です。

**`onVendorData()` は短く保ってください。** 各スロットは自身の完了から再 submit されますが、callback はその前に呼ばれるため、callback が返るまでそのスロットは飛んでいない状態のままです。ring buffer へ複製して、処理は別の場所で行ってください。[コールバックのコンテキスト](../../../docs/usb-host-advanced.ja.md#8-コールバックのコンテキスト) を参照してください。

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
