# EspUsbHostUsbtmcScpi

> English: [README.md](README.md)

USBTMC 計測器に SCPI で話すサンプルです。機器の識別、設定値の書き込みと読み戻し、実測値の取得
を行います。菊水電子工業の直流電源 PMX18-5A (`0b3e:1029`) で検証済みです。

> **状態: 動作確認済み（ESP32-S3）。** 以下のバイト列は `tests/manual/usbtmc_scpi`
> で実機に対して確認しました。出力は [実測結果](#実測結果) にあります。

| ファイル | 内容 |
|---|---|
| `UsbtmcProtocol.hpp` | ワイヤフォーマット。12 バイトの bulk メッセージヘッダ、4 バイト境界パディング、bTag 規則、class request のコード、`GET_CAPABILITIES` の構造。Arduino / USB 依存なし |
| `UsbtmcDevice.hpp` | USBTMC interface の検出、vendor bulk API による claim、EP0 の class request、bulk endpoint でのメッセージ送受信。SCPI も VID/PID も含まない |
| `ScpiPmx.hpp` | 機種固有層。PMX シリーズ用の SCPI コマンド。他機種に向けるときはこのファイルを差し替える |
| `EspUsbHostUsbtmcScpi.ino` | 接続 → capabilities と `*IDN?` の表示 → 設定 → 実測値の定期取得 |

## なぜ `examples/Vendor/` にあるのか

**USBTMC の interface class は `0xfe` (Application Specific) / subclass `0x03` で、
vendor-specific class `0xff` ではありません。**

`examples/` のカテゴリは「デバイスの USB class」ではなく **「ライブラリ本体のどの
API を使う example か」** で分かれています。`Vendor/` は「vendor bulk/control API の
上に作られた example」を意味し、この example がまさにそれです（本体に USBTMC 対応
はありません）。隣も同じで、`EspUsbHostDisplayAx206` は実質 MSC 系の Bulk-Only Transport
を話し、`EspUsbHostDisplayTuring` は CDC API を使うので `Serial/` にあります。

クラス名のディレクトリ（`Ccid/` / `UsbNetwork/` / `Storage/` / `Audio/`）は、本体に
そのクラス専用の API がある場合にだけ存在します。

## 仕組み

USBTMC は「2 本の bulk endpoint 上のメッセージ層」+「EP0 の class request 群」です。
SCPI テキストはそのペイロードで、クラス自身は中身に関与しません。

### ライブラリ側

1 つを除いて、すべて vendor-specific デバイス用に既にあった API で足ります。

```cpp
// interface 番号を明示するので、vendor-specific でない class 0xfe でも claim される。
// READ_ON_DEMAND は request/response 型のプロトコルに合う（継続 IN 転送だと NAK し
// 続けるだけになる）。
usb.vendorOpen(address, interfaceNumber, ESP_USB_HOST_VENDOR_READ_ON_DEMAND);
usb.vendorSetAutoZlp(true, address);   // OUT 転送を必ず short packet で終端する
usb.vendorWrite(message, length, address);
usb.vendorReadSync(buffer, sizeof(buffer), &received, timeoutMs, address);
```

この機器には interrupt IN endpoint（USB488 の service request 用）もありますが、この
example では開きません。SRQ の用途は `*OPC?` のポーリングで代替できます。

本体への追加は `vendorControlTransfer()` の 1 本だけで、`bmRequestType` を引数で
渡せます。`vendorControlIn()` / `vendorControlOut()` は `0xc0` / `0x40`（vendor 型・
device recipient）固定ですが、USBTMC の class request はすべて `0xa1` の
**interface** 宛です。

```cpp
usb.vendorControlTransfer(0xa1, usbtmc::REQ_GET_CAPABILITIES, 0, interfaceNumber,
                          data, sizeof(data), &received, address);
```

### bulk メッセージヘッダ

両方向とも先頭 12 バイトで、全体長が 4 の倍数になるまで `0x00` でパディングします。

| offset | 内容 |
|---|---|
| 0 | `MsgID`: 1 = DEV_DEP_MSG_OUT, 2 = REQUEST_DEV_DEP_MSG_IN / DEV_DEP_MSG_IN |
| 1-2 | `bTag`（1..255、0 不可、直前と同じ値も不可）とそのビット反転 |
| 4-7 | `TransferSize`（リトルエンディアン） |
| 8 | `bmTransferAttributes`: OUT では EOM、IN 要求では TermCharEnabled |
| 9 | IN 要求のときの `TermChar` |

したがって `*IDN?` は 20 バイトで出ていきます。

```
01 01 fe 00  05 00 00 00  01 00 00 00  2a 49 44 4e 3f 00 00 00
                                       *  I  D  N  ?  <padding>
```

クエリは 2 メッセージです。コマンドを載せた DEV_DEP_MSG_OUT、続いて
REQUEST_DEV_DEP_MSG_IN と bulk IN 読み出しです。要求したチャンクに収まらない応答は
EOM が下りた状態で届き、次の要求で継続されます。

### CLEAR シーケンスと、この example が省いた 1 手順

`INITIATE_CLEAR` の後、`CHECK_CLEAR_STATUS` をデバイスが完了を返すまでポーリング
します。デバイスが「まだ bulk IN にデータがある」と言っている間は読み捨てます。

USBTMC はこのシーケンスの最後に bulk OUT への `CLEAR_FEATURE(ENDPOINT_HALT)` を
置きます。`UsbtmcDevice::clearOutHalt()` に実装はありますが、どこからも呼んでいません。
halt をクリアするとデバイス側のデータトグルがリセットされますが、このホストは自分側を
合わせられません（ESP-IDF のホストスタックは実際に halt した pipe しか再同期しないため
です）。PMX18-5A での実測では、このリクエストを入れると CLEAR 直後のクエリがタイム
アウトし、外すと CLEAR は成功して次のクエリも応答します。本当に stall した endpoint
は、転送失敗時に pipe を flush / clear するライブラリ本体の経路が回復させます。

### GET_CAPABILITIES のオフセット注意

USB488 の capability バイトは 12 / 13 ではなく **14 / 15** にあります（12-13 は
`bcdUSB488`）。2 バイト手前を読むと、全機能対応の機器が「何も対応していない」ように
見えます。実際にここでそうなり、生バイトをダンプして判明しました。`tests/unit/usbtmc`
が PMX18-5A の実応答でこのレイアウトを固定しています。

## 実測結果

`tests/manual/usbtmc_scpi`（ESP32-S3、PMX18-5A をフルスピードハブ経由で接続）:

```
usbtmc interface address=2 interface=0 protocol=0x01
capabilities raw:01 00 00 01 00 01 00 00 00 00 00 00 00 01 07 0F 00 00 00 00 00 00 00 00
capabilities usbtmc=0100 usb488=0100 scpi=1 usb488.2=1 indicator=0 termchar=1
idn KIKUSUI,PMX18-5A,DR000046,IFC01.56.0015 IOC01.10.0070
setting readback 3.300V 0.250A
measured -0.004V 0.000A
output OFF
repeated queries done
error queue 0 "No error"
```

デバイスの descriptor（`tests/manual/device_dump`）:

```
VID:PID 0b3e:1029 class=0x00(per-interface)
Strings manufacturer="KIKUSUI" product="PMX18-5A" serial="DR000046"
  Interface 0 alt=0 class=0xfe subclass=0x03 protocol=0x01 endpoints=3
    Endpoint iface=0 ep=0x01 dir=OUT type=bulk      max_packet=64
    Endpoint iface=0 ep=0x82 dir=IN  type=bulk      max_packet=64
    Endpoint iface=0 ep=0x83 dir=IN  type=interrupt max_packet=16 interval=100
```

## 使用している SCPI コマンド

IEEE 488.2 共通コマンドと SCPI 標準の電源系ノードのみを使います。多くは他の
programmable 電源でもそのまま通ります。

| 用途 | コマンド |
|---|---|
| 識別 | `*IDN?` |
| ステータスクリア | `*CLS` |
| 出力電圧 | `VOLT <v>` / `VOLT?` |
| 出力電流 | `CURR <a>` / `CURR?` |
| 出力 ON/OFF | `OUTP ON` / `OUTP OFF` / `OUTP?` |
| 実測値 | `MEAS:VOLT?` / `MEAS:CURR?` |
| エラーキュー | `SYST:ERR?` |
| 完了待ち | `*OPC?` |

`SYST:ERR?` が `0,"No error"` を返すことは、送ったコマンドすべてが受理された
という機器自身の判定です。新機種の立ち上げで最も役に立つチェックです。

## 安全上の注意

このスケッチは電源の出力**設定値**を変更します。出力の ON は `TURN_OUTPUT_ON` を
`true` にしたときだけ行います（既定は `false`）。動作を確認するまでは出力端子に何も
接続しない状態で実行してください。manual テストは出力を ON にしません。

## 他機種への流用

`ScpiPmx.hpp` を差し替えます。`UsbtmcProtocol.hpp` と `UsbtmcDevice.hpp` には SCPI も
VID/PID も機種の上限値も入っていないので、DMM やオシロなら自身のコマンドセットと、
（USBTMC 機器が複数ある場合に識別が必要なら）VID/PID を `UsbtmcDevice::begin()` に
渡すだけで済みます。

新機種で確認すべき点は 2 つあります。IN 要求で `TermChar` を有効にする必要があるか
（PMX は不要）、そして応答が 1 ラウンドあたり `usbtmc::RESPONSE_CHUNK` を超えるか
（超えても読み出しのラウンド数が増えるだけ）です。

## 参照

- USB-IF, *Universal Serial Bus Test and Measurement Class Specification (USBTMC)*, Revision 1.0
- USB-IF, *USBTMC USB488 Subclass Specification*, Revision 1.0
- IEEE 488.2 共通コマンド、SCPI 1999 標準コマンド
- 菊水電子工業 PMX シリーズ 通信インターフェースマニュアル

これらの公開仕様から実装しました。GPL ライセンスの USBTMC ドライバは参照していません。

> KIKUSUI and PMX are trademarks of KIKUSUI ELECTRONICS CORPORATION. This project
> is not affiliated with, endorsed by, or certified by KIKUSUI ELECTRONICS
> CORPORATION.
