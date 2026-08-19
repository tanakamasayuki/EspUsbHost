# EspUsbHostProtocolConsole

> English: [README.md](README.md)

仕様が公開されていないUSBプロトコルを解析するための対話コンソールです。シリアルモニタからUSB転送を入力し、デバイスの応答をその場で確認できます。試行ごとに再ビルドする必要はありません。

想定する手順は次のとおりです。PC上でUSBPcap + Wireshark（Linuxなら `usbmon`）でキャプチャし、その中の1つの転送を選び、ここでバイト単位に再現します。応答がキャプチャと一致すれば、そのプロトコルの一部は理解できたことになり、スケッチに書き起こせます。[docs/usb-host-guide.ja.md](../../../docs/usb-host-guide.ja.md) の手順の一部です。

## ハードウェア

- ESP32-S2 / ESP32-S3 / ESP32-P4
- プロトコルを解析したいUSBデバイス

## コマンド

1行1コマンドです。数値はすべて16進で、`0x` 接頭辞は任意です。

| コマンド | 説明 |
|----------|------|
| `help` | コマンド一覧を表示 |
| `list` | デバイス／インターフェース／エンドポイント一覧（現在の対象に印が付く） |
| `addr <a>` | 対象デバイスアドレスを選択（既定: 最初のデバイス） |
| `open <iface> [ondemand]` | バルク転送のためにインターフェースをclaim。`ondemand` は `in` が要求するまでバルクINエンドポイントを動かさない設定で、要求／応答型プロトコルではこちらが必要 |
| `ctl <bmRequestType> <bRequest> <wValue> <wIndex> <len\|bytes...>` | EP0コントロール転送を1回。`bmRequestType` のbit7が1ならIN（長さを指定）、0ならOUT（データを指定、省略可） |
| `out <bytes...>` | バルクOUT |
| `in [len] [timeout_ms]` | バルクINを1回、完了まで待つ（既定64バイト、1000ms） |
| `zlp` | 長さ0のバルクOUTパケット。転送終端にZLPを要求するプロトコル向け |
| `mon on\|off` | 要求せずに届くバルクINデータの表示（continuousモード） |
| `desc` | 対象デバイスの生コンフィグレーションディスクリプタ |

## セッション例

```
> list
device address=1 0483:070b "Xprinter" "Printer"  <= target
  interface 0 class=0x07/0x01/0x02 claimed=no
    ep 0x01 OUT attrs=0x02 max_packet=64 interval=0
    ep 0x82 IN  attrs=0x02 max_packet=64 interval=0

> ctl 80 06 0100 0000 12
ctl type=0x80 req=0x06 value=0x0100 index=0x0000 len=18: ok (2ms)
  0000  12 01 00 02 00 00 00 40 83 04 0b 07 00 01 01 02  |.......@........|
  0010  00 01                                            |..|

> open 0
open iface=0 mode=continuous: ok
  bulk out ep=0x01 mps=64 / bulk in ep=0x82 mps=64

> out 10 04 01
out len=3: ok
in  address=1 iface=0 ep=0x82 len=1
  0000  16                                               |.|
```

`ctl 80 06 0100 0000 12` は標準の `GET_DESCRIPTOR(DEVICE)` です。`bmRequestType=0x80`（IN、標準、宛先はデバイス）、`bRequest=0x06`、`wValue=0x0100`（ディスクリプタタイプ1、インデックス0）、18バイト。コンソールがデバイスに届いていることを確認する、最初に打つのに最も安全なコマンドです。

## 失敗の読み方

失敗もまた情報です。単なるエラーではありません。

- **`ctl` が失敗する** — デバイスがその要求をSTALLした、つまり未サポートという意味です。よくある原因は `bmRequestType` の宛先違い（デバイス宛かインターフェース宛か）と `wIndex` の誤りです。クラス要求は通常インターフェース宛なので、`wIndex` はインターフェース番号になります。
- **`open` が失敗する** — そのインターフェースをライブラリ自身のクラスドライバが既にclaimしているか、インターフェース番号が存在しません。`list` でライブラリが持っているインターフェースは `claimed=yes` と表示されます。
- **`in` がタイムアウトする** — 多くのデバイスはトランザクション内でしか応答しません。`ondemand` で開き、まず `out` で要求を送ってから読み出してください。
- **`out` が失敗する** — インターフェースが開いていないか、以前のエラーでエンドポイントがhaltしています。デバイスを挿し直すと解除されます。

## 制限

- コントロール転送1回で運べるデータは最大248バイト（256バイトから8バイトのsetupを引いた値）です。ESP-IDFホストスタックの制限です。
- `out` と `in` は本スケッチのバッファにより1コマンドあたり512バイトまでです。
- このコンソールが扱うのはバルクとコントロール転送です。アイソクロナスエンドポイント（オーディオ、ビデオ）はここからは扱えません。

## 主要API

- `usb.vendorOpen(address, interfaceNumber, readMode)` — クラスを問わず任意のインターフェースをclaim
- `usb.vendorControlTransfer(...)` — 呼び出し側が `bmRequestType` を指定するEP0転送
- `usb.vendorWrite()` / `usb.vendorReadSync()` / `usb.vendorWriteZlp()`
- `usb.onVendorData()` — continuousモードでのバルクINペイロード
