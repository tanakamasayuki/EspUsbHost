# EspUsbHostDeviceExplorer

> English: [README.md](README.md)

未知のUSBデバイスを識別し、どう扱うかを決めるためのツールです。接続された各デバイスについて、解析済みディスクリプタダンプ、インターフェースごとの「そのクラスが何で、どのEspUsbHost APIやサンプルで扱えるか」、そして生のディスクリプタバイト列を表示します。生のバイト列には、解析済みダンプでは見えないクラス固有ディスクリプタ（HID / CDC機能 / CCID / UAC）も含まれます。

生ダンプの部分は、PC側で取ったUSBPcap/Wiresharkのキャプチャや `lsusb -v` の出力と突き合わせるためのものです。[docs/usb-host-guide.ja.md](../../../docs/usb-host-guide.ja.md) の手順の一部です。

## ハードウェア

- ESP32-S2 / ESP32-S3 / ESP32-P4
- 調査したいUSBデバイス

## 動作内容

- 列挙が落ち着くのを待ってから自動でダンプ（コンポジットデバイスは複数のイベントを上げるため）
- `printAllDeviceInfo()` — 解析済みのデバイス／インターフェース／エンドポイント／チャネル状態
- インターフェースごとに、クラスの意味、それを扱うAPIまたはサンプル、方向・転送タイプ・最大パケットサイズ・intervalつきのエンドポイント一覧
- `GET_DESCRIPTOR(DEVICE)` と `GET_DESCRIPTOR(CONFIGURATION)` の生バイトをHex/ASCIIでダンプ
- コンフィグレーションディスクリプタのブロック単位の走査。offset、`bLength`、名前つきの `bDescriptorType`、各ブロックの生バイト

## シリアルコマンド

| キー | 動作 |
|------|------|
| `d` | 全体を再ダンプ |
| `r` | 生ディスクリプタのみ |

## コントロール転送の制限

ESP-IDFのプリコンパイル済みホストスタックは、8バイトのsetupを含めてコントロール転送1回を256バイトに制限しています。したがって1回の要求で読めるディスクリプタは最大248バイトです。`wTotalLength` がこれを超える場合、その旨を表示して先頭248バイトをダンプします。これはUSBカメラ（UVC）がそもそも列挙できないのと同じ制限です。コンフィグレーションディスクリプタが256バイトを超えるデバイスは、クラスドライバが動く前の列挙段階で失敗します。

## 主要API

- `usb.printAllDeviceInfo()` — 全デバイスの解析済みダンプ
- `usb.getDevices()` / `usb.getInterfaces()` / `usb.getEndpoints()` — ディスクリプタ状態をデータとして取得
- `usb.vendorControlTransfer(0x80, 0x06, ...)` — EP0への標準 `GET_DESCRIPTOR`。EP0はインターフェースではなくデバイスに属するので、`vendorOpen()` なしで使えます
- `usb.onDeviceConnected()` / `usb.onDeviceDisconnected()`

## 期待されるシリアル出力

```
connected address=1 045e:07a5 "USB Keyboard"

=========== USB Device ===========
...printAllDeviceInfo() の出力...
========= USB Device End =========

--- how to drive address=1 (045e:07a5) ---
interface 0 alt=0 class=0x03/0x01/0x01 claimed=yes
  what : HID boot keyboard
  how  : library API: onKeyboard() -- examples/HID/EspUsbHostKeyboard
  ep   : 0x81 IN  interrupt   max_packet=8 interval=10
interface 1 alt=0 class=0x03/0x00/0x00 claimed=yes
  what : HID (report protocol)
  how  : library API: onHIDInput()/onHIDVendorInput() -- examples/HID/EspUsbHostHIDRawDump
  ep   : 0x82 IN  interrupt   max_packet=8 interval=10

--- raw DEVICE descriptor (address=1) ---
  0000  12 01 00 02 00 00 00 08 5e 04 a5 07 01 01 01 02  |........^.......|
  0010  00 01                                            |..|
--- raw CONFIGURATION descriptor (address=1) ---
  wTotalLength=59 read=59 bytes
  0000  09 02 3b 00 02 01 00 a0 32 09 04 00 00 01 03 01  |..;.....2.......|
  ...
--- configuration descriptor blocks ---
  offset=  0 len= 9 type=0x02 CONFIGURATION                            09 02 3b 00 02 01 00 a0 32
  offset=  9 len= 9 type=0x04 INTERFACE                                09 04 00 00 01 03 01 01 00
  offset= 18 len= 9 type=0x21 HID / CDC-or-class-specific (0x21)       09 21 11 01 00 01 22 41 00
  offset= 27 len= 7 type=0x05 ENDPOINT                                 07 05 81 03 08 00 0a
  ...

=== end of dump ('d' dump again, 'r' raw descriptors only) ===
```

## 次のステップ

- HIDデバイス: [EspUsbHostHIDReportDescriptor](../EspUsbHostHIDReportDescriptor/) でレポート構造を確認し、続いて [EspUsbHostHIDRawDump](../../HID/EspUsbHostHIDRawDump/)
- 未知のバルクプロトコル: [EspUsbHostProtocolConsole](../../Vendor/EspUsbHostProtocolConsole/) で転送を手で試す
