# EspUsbHostUsbtmcScpi

English: [README.md](README.md)

USBTMC 計測器に SCPI で話す例。機器の識別、設定値の書き込みと読み戻し、実測値の取得
を行う。菊水電子工業の直流電源 PMX18-5A (`0b3e:1029`) で検証済み。

> **状態: 動作確認済み（ESP32-S3）。** 以下のバイト列は `tests/manual/usbtmc_scpi`
> で実機に対して確認した。出力は [実測結果](#実測結果) にある。

| ファイル | 内容 |
|---|---|
| `UsbtmcProtocol.hpp` | ワイヤフォーマット。12 バイトの bulk メッセージヘッダ、4 バイト境界パディング、bTag 規則、class request のコード、`GET_CAPABILITIES` の構造。Arduino / USB 依存なし |
| `UsbtmcDevice.hpp` | USBTMC interface の検出、vendor bulk API による claim、EP0 の class request、bulk endpoint でのメッセージ送受信。SCPI も VID/PID も含まない |
| `ScpiPmx.hpp` | 機種固有層。PMX シリーズ用の SCPI コマンド。他機種に向けるときはこのファイルを差し替える |
| `EspUsbHostUsbtmcScpi.ino` | 接続 → capabilities と `*IDN?` の表示 → 設定 → 実測値の定期取得 |

## なぜ `examples/Vendor/` にあるのか

**USBTMC の interface class は `0xfe` (Application Specific) / subclass `0x03` で、
vendor-specific class `0xff` ではない。**

`examples/` のカテゴリは「デバイスの USB class」ではなく **「ライブラリ本体のどの
API を使う example か」** で分かれている。`Vendor/` は「vendor bulk/control API の
上に作られた example」を意味し、この example がまさにそれである（本体に USBTMC 対応
は無い）。隣も同じで、`EspUsbHostDisplayAx206` は実質 MSC 系の Bulk-Only Transport
を話し、`EspUsbHostDisplayTuring` は CDC API を使うので `Serial/` にある。

クラス名のディレクトリ（`Ccid/` / `UsbNetwork/` / `Storage/` / `Audio/`）は、本体に
そのクラス専用の API がある場合にだけ存在する。

## 仕組み

USBTMC は「2 本の bulk endpoint 上のメッセージ層」+「EP0 の class request 群」で
ある。SCPI テキストはそのペイロードで、クラス自身は中身に関与しない。

### ライブラリ側

1 つを除いて、すべて vendor-specific デバイス用に既にあった API で足りる。

```cpp
// interface 番号を明示するので、vendor-specific でない class 0xfe でも claim される。
// READ_ON_DEMAND は request/response 型のプロトコルに合う（継続 IN 転送だと NAK し
// 続けるだけになる）。
usb.vendorOpen(address, interfaceNumber, ESP_USB_HOST_VENDOR_READ_ON_DEMAND);
usb.vendorSetAutoZlp(true, address);   // OUT 転送を必ず short packet で終端する
usb.vendorWrite(message, length, address);
usb.vendorReadSync(buffer, sizeof(buffer), &received, timeoutMs, address);
```

この機器には interrupt IN endpoint（USB488 の service request 用）もあるが、この
example では開かない。SRQ の用途は `*OPC?` のポーリングで代替できる。

本体への追加は `vendorControlTransfer()` の 1 本だけ。`bmRequestType` を引数で
渡せる。`vendorControlIn()` / `vendorControlOut()` は `0xc0` / `0x40`（vendor 型・
device recipient）固定だが、USBTMC の class request はすべて `0xa1` の
**interface** 宛である。

```cpp
usb.vendorControlTransfer(0xa1, usbtmc::REQ_GET_CAPABILITIES, 0, interfaceNumber,
                          data, sizeof(data), &received, address);
```

### bulk メッセージヘッダ

両方向とも先頭 12 バイト。全体長が 4 の倍数になるまで `0x00` でパディングする。

| offset | 内容 |
|---|---|
| 0 | `MsgID`: 1 = DEV_DEP_MSG_OUT, 2 = REQUEST_DEV_DEP_MSG_IN / DEV_DEP_MSG_IN |
| 1-2 | `bTag`（1..255、0 不可、直前と同じ値も不可）とそのビット反転 |
| 4-7 | `TransferSize`（リトルエンディアン） |
| 8 | `bmTransferAttributes`: OUT では EOM、IN 要求では TermCharEnabled |
| 9 | IN 要求のときの `TermChar` |

したがって `*IDN?` は 20 バイトで出ていく。

```
01 01 fe 00  05 00 00 00  01 00 00 00  2a 49 44 4e 3f 00 00 00
                                       *  I  D  N  ?  <padding>
```

クエリは 2 メッセージ。コマンドを載せた DEV_DEP_MSG_OUT、続いて
REQUEST_DEV_DEP_MSG_IN と bulk IN 読み出し。要求したチャンクに収まらない応答は
EOM が下りた状態で届き、次の要求で継続される。

### CLEAR シーケンスと、この example が省いた 1 手順

`INITIATE_CLEAR` の後、`CHECK_CLEAR_STATUS` をデバイスが完了を返すまでポーリング
する。デバイスが「まだ bulk IN にデータがある」と言っている間は読み捨てる。

USBTMC はこのシーケンスの最後に bulk OUT への `CLEAR_FEATURE(ENDPOINT_HALT)` を
置く。`UsbtmcDevice::clearOutHalt()` に実装はあるが、どこからも呼んでいない。halt を
クリアするとデバイス側のデータトグルがリセットされるが、このホストは自分側を合わせ
られない（ESP-IDF のホストスタックは実際に halt した pipe しか再同期しない）。
PMX18-5A で実測: このリクエストを入れると CLEAR 直後のクエリがタイムアウトする。
外すと CLEAR は成功し、次のクエリも応答する。本当に stall した endpoint は、転送
失敗時に pipe を flush / clear するライブラリ本体の経路が回復させる。

### GET_CAPABILITIES のオフセット注意

USB488 の capability バイトは 12 / 13 ではなく **14 / 15** にある（12-13 は
`bcdUSB488`）。2 バイト手前を読むと、全機能対応の機器が「何も対応していない」ように
見える。実際にここでそうなり、生バイトをダンプして判明した。`tests/unit/usbtmc` が
PMX18-5A の実応答でこのレイアウトを固定している。

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

IEEE 488.2 共通コマンドと SCPI 標準の電源系ノードのみ。多くは他の programmable
電源でもそのまま通る。

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
という機器自身の判定である。新機種の立ち上げで最も役に立つチェック。

## 安全上の注意

このスケッチは電源の出力**設定値**を変更する。出力の ON は `TURN_OUTPUT_ON` を
`true` にしたときだけ行う（既定は `false`）。動作を確認するまでは出力端子に何も
接続しない状態で実行すること。manual テストは出力を ON にしない。

## 他機種への流用

`ScpiPmx.hpp` を差し替える。`UsbtmcProtocol.hpp` と `UsbtmcDevice.hpp` には SCPI も
VID/PID も機種の上限値も入っていないので、DMM やオシロなら自身のコマンドセットと、
（USBTMC 機器が複数ある場合に識別が必要なら）VID/PID を `UsbtmcDevice::begin()` に
渡すだけでよい。

新機種で確認すべき点は 2 つ。IN 要求で `TermChar` を有効にする必要があるか（PMX は
不要）、応答が 1 ラウンドあたり `usbtmc::RESPONSE_CHUNK` を超えるか（超えても
読み出しのラウンド数が増えるだけ）。

## 参照

- USB-IF, *Universal Serial Bus Test and Measurement Class Specification (USBTMC)*, Revision 1.0
- USB-IF, *USBTMC USB488 Subclass Specification*, Revision 1.0
- IEEE 488.2 共通コマンド、SCPI 1999 標準コマンド
- 菊水電子工業 PMX シリーズ 通信インターフェースマニュアル

これらの公開仕様から実装した。GPL ライセンスの USBTMC ドライバは参照していない。

> KIKUSUI and PMX are trademarks of KIKUSUI ELECTRONICS CORPORATION. This project
> is not affiliated with, endorsed by, or certified by KIKUSUI ELECTRONICS
> CORPORATION.
