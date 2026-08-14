# EspUsbHost USB Printer クラス (ESC/POS レシートプリンタ) 対応 仕様

> **English readers:** analysis notes for the USB Printer class (`0x07`) and ESC/POS,
> written while implementing the receipt printer example against an Xprinter XP-C58K
> (`0483:070b`). The usable summary in English — the three class requests, why
> `DLE EOT n` is the status path on this model, and the print sequence — is in
> [`examples/Vendor/EspUsbHostPrinterEscPos/README.md`](../examples/Vendor/EspUsbHostPrinterEscPos/README.md).

## 目的

USB Printer クラス（interface class `0x07`）のレシートプリンタを EspUsbHost から扱えるようにする。開発対象は Xprinter XP-C58K（`0483:070b`）で、日本語フォント ROM とオートカッターを持つ 58mm サーマルプリンタ。

プリンタ処理はライブラリ本体に入れない。**本体への追加は表示用の 1 行だけ**（後述）。クラス要求層・印字言語層・機種内容層はすべて `examples/` 側に置く。この分割方針は `usbtmc-spec.ja.md` / `dp100-spec.ja.md` / `usb-display-spec.ja.md` / `vendor-api-spec.ja.md` と同じ立場である。

## カテゴリは `examples/Vendor/`

`examples/` のカテゴリは「デバイスの USB class」ではなく **「ライブラリ本体のどの API を使う example か」** で分かれている。プリンタはベンダバルク/コントロール API で駆動するので `examples/Vendor/` に置く。**クラスは `0x07` であって `0xff` ではない**が、ディレクトリ名はクラスではなく API を指している。

同じ立場の先例が USBTMC（class `0xfe`、`examples/Vendor/EspUsbHostUsbtmcScpi/`）。クラス名のディレクトリ（`Ccid/`、`UsbNetwork/`、`Storage/`、`Audio/`）は、ライブラリにそのクラス専用 API がある場合だけ存在する。

README（en/ja）の冒頭で「USB Printer クラス / ESC/POS である」ことを明示してフォローする。

## 対象デバイスの実測 descriptor

`tests/manual/device_dump` の出力（ハブ経由、full-speed）。

```
Address 2 portId=0x11 parent=1 hub_index=1 upstream_port=1 speed=full-speed
VID:PID 0483:070b class=0x00(per-interface) subclass=0x00 protocol=0x00
Supported=no hub=no
USB 2.00 device 1.00 ep0=64
Strings manufacturer="Xprinter " product="" serial=""
Configuration value=1 interfaces=1 total_len=32 attributes=0xc0(self-powered) max_power=32mA
  Interface 0 alt=0 class=0x07(Printer) subclass=0x01 protocol=0x02 endpoints=2
    Endpoint iface=0 ep=0x01 dir=OUT type=bulk max_packet=64 interval=0 attrs=0x02
    Endpoint iface=0 ep=0x82 dir=IN  type=bulk max_packet=64 interval=0 attrs=0x02
```

読み取れること。

- interface 1 本、class `0x07` / subclass `0x01`（printer）/ protocol `0x02`（**双方向**）。protocol `0x01` なら単方向でバルク IN が無く、ステータスをデータ経路から読む手段が最初から無い
- バルク OUT `0x01` と バルク IN `0x82`、どちらも 64 バイト。印字データは OUT、ステータスは IN
- `product=""` / `serial=""`。列挙時に `ENUM: Device returned less bytes than requested` が 2 回出るのはこれが原因で、申告した文字列ディスクリプタを短く返している。無害（列挙は続行する）
- `Supported=no` は対応前の出力。ライブラリのクラスドライバが掴む対象ではなく、ベンダ API で明示 claim する

## 一次情報とライセンス

Printer クラスと ESC/POS はどちらも公開仕様があるので、逆解析は不要。

| 参照元 | ライセンス | 扱い |
|---|---|---|
| USB-IF *Device Class Definition for Printing Devices* 1.1 | 公開仕様 | **一次情報**（クラス要求・subclass/protocol コード） |
| IEEE 1284-2000 デバイス ID 文字列形式 | 公開仕様 | **一次情報** |
| Epson *ESC/POS Command Reference* | 公開資料 | **一次情報**（`ESC`/`GS`/`FS`/`DLE EOT` 各コマンド） |
| 実機実測（`tests/probe/printer_class`, `tests/manual/printer_*`） | — | **一次情報**。機種固有の挙動はこちらだけが根拠 |
| GPL ライセンスのプリンタドライバ | GPL | **参照しない** |

本ライブラリは MIT。GPL 実装のコードは取り込まない。

## 命名と商標

「Xprinter」は各権利者の商標、「Epson」「ESC/POS」はセイコーエプソン株式会社の商標。指名的使用（対応ハードウェアの特定）に限る。

- example README に商標表記を置く
- 汎用層の識別子は `printer::` / `escpos::`、機種内容層は `receipt::`
- 「対応」「認定」など認証を示唆する表現は使わない

## ライブラリ本体への追加

**API 追加はゼロ。** 必要なものは既存 API で足りる。

| 用途 | 使う既存 API |
|---|---|
| interface 0（class 0x07）を claim | `vendorOpen(addr, 0, ESP_USB_HOST_VENDOR_READ_ON_DEMAND)` — 番号を明示すればクラス不問 |
| 印字データ送出 | `vendorWrite()`（大量なら `vendorWriteAsync()` キュー） |
| ステータス読み取り | `vendorReadSync()` |
| 短パケット終端 | `vendorSetAutoZlp(true, addr)` |
| クラス要求 3 種 | `vendorControlTransfer()`（2.7.4 で USBTMC 対応時に追加済み） |

唯一の本体変更は表示用。`className()` に以下を追加した。

```cpp
case 0x07: return "Printer";     // ESC/POS レシートプリンタ、IPP/raw プリンタ
case 0x0b: return "Smart Card";  // CCID（ライブラリに専用 API があるのに名前が無かった）
```

`printDeviceInfo()` / `device_dump` が `class=0x07(Unknown)` と出していたのを直すだけで、挙動は変わらない。

## 層の分割

| ファイル | 層 | 依存 |
|---|---|---|
| `PrinterProtocol.hpp` | USB Printer クラス（要求 3 種、ポートステータス、IEEE 1284 デバイス ID） | なし（Arduino / USB 非依存） |
| `EscPos.hpp` | 印字言語（コマンドビルダ、リアルタイムステータス応答） | なし（同上） |
| `PrinterDevice.hpp` | USB との接続（探索・claim・EP0・バルク） | `EspUsbHost.h` |
| `ReceiptJa.hpp` | 印字内容（日本語レシート、ASCII 伝票） | `EscPos.hpp` |
| `.ino` | 制御フロー | 上記 |

上 2 層が Arduino / USB 非依存なので、`tests/unit/escpos` が **production ヘッダをそのまま g++ でコンパイルして**検査できる（抽出スクリプトを介さない）。USBTMC / DP100 と同じ構成。

## プロトコル

### クラス要求

| 要求 | bmRequestType | bRequest | wValue | wIndex |
|---|---|---|---|---|
| GET_DEVICE_ID | `0xa1` | `0x00` | コンフィグレーションインデックス | **`(interface << 8) \| alt`** |
| GET_PORT_STATUS | `0xa1` | `0x01` | 0 | interface |
| SOFT_RESET | `0x21` | `0x02` | 0 | interface |

GET_DEVICE_ID の wIndex だけバイト順が他と逆。呼び出し側に書かせず `printer::deviceIdIndex()` に持たせる。

デバイス ID は **ビッグエンディアン 2 バイトの長さ（自身を含む）** ＋ `KEY:value;` の並び。キーは長短両方の綴りを試す（`MFG`/`MANUFACTURER`、`MDL`/`MODEL`、`CMD`/`COMMAND SET`）。

### GET_PORT_STATUS のビット

| bit | 意味 |
|---|---|
| 5 | PaperEmpty（**用紙が無いときに 1**） |
| 4 | Select（選択されているときに 1） |
| 3 | NotError（**エラーが無いときに 1**） |

### ESC/POS 側

| 用途 | コマンド |
|---|---|
| 初期化 | `ESC @` |
| コードページ / 漢字コード系 / 漢字モード | `ESC t n` / `FS C n` / `FS &`・`FS .` |
| 寄せ・強調・下線・反転・サイズ | `ESC a n`・`ESC E n`・`ESC - n`・`GS B n`・`GS ! n` |
| 紙送り / カット | `LF`・`ESC d n` / `GS V m [n]` |
| バーコード / QR / ラスタ | `GS k m n …` / `GS ( k …` / `GS v 0 …` |
| リアルタイムステータス | `DLE EOT n`（n = 1 プリンタ / 2 オフライン要因 / 3 エラー / 4 用紙） |

`DLE EOT` の応答は bit 0 = 0、bit 1 = 1 固定。この 2 ビットで「ステータスバイトかどうか」を判定する。

### 日本語

2 バイト文字は **Shift-JIS**（`FS C 1`）でフォント ROM から出す。UTF-8 は解さない。サンプル文はビルド時に変換した Shift-JIS バイト配列として持つ（文字列リテラルにすると、Shift-JIS の 2 バイト目が 16 進数字のとき `"\x82"` の直後が同じ `\x` エスケープに吸われる）。任意テキストが必要なら部分的な変換表か `GS v 0` のラスタ画像。

## 実測で判明したこと

いずれも実機だけが根拠で、仕様書からは出てこない。`tests/probe/printer_class` と `tests/unit/escpos` の回帰ベクタとして固定した。

1. **クラス要求は「応答するが中身が無い」ことがある。**
   XP-C58K の `GET_DEVICE_ID` は `00 02`、すなわち**形式は正しい空の ID** を返す。`GET_PORT_STATUS` は常に `0x00`。どちらも STALL しないので、ホストからは「未実装」と「報告する事が無い」を返り値の中身以外では区別できない。
   実装の初版はどちらも失敗扱いにしていたため、正常なプリンタを壊れていると報告した。probe で wValue（0/1）・wIndex のバイト順・recipient（interface/device）・request type（class/vendor）を総当たりし、**spec どおりの形式だけが応答し、他は STALL する**ことを確認して切り分けた。
   → `decodeDeviceId()` は「形式が正しいか」を返し、文字数は別の出力にした。空の ID は成功。

2. **ポートステータスの `0x00` は「情報なし」として扱う。**
   文字どおりなら「非選択・エラー・用紙あり」だが、EP0 に応答して正常に印字しているプリンタがその状態にあるはずがない。他のやり取りの前後でも `SOFT_RESET` 後でも毎回 `0x00`、その間リアルタイムステータスは正常。文字どおり読むと健全なプリンタで印字を拒否する。
   → `decodePortStatus()` に `unknown` を設け、`0x00` では他フィールドを立てない。代償（本当に非選択＋エラーでちょうど `0x00` の状態が unknown に見える）は許容する。呼び出し側は実際に答えが返る経路にフォールバックできる方が安全。

3. **頼れるステータス経路は `DLE EOT`。** 連続 20/20 応答、586 バイトの印字転送直後でも 5/5 応答。印字バッファより先に処理されるので印字中でも答える。

4. **`SOFT_RESET` はデータトグルを壊さなかった。** 実行後も EP0・バルクとも正常。USBTMC で `CLEAR_FEATURE(ENDPOINT_HALT)` を外す必要があった件があるので、毎回確認する項目にしている。

5. **日本語フォント ROM は Shift-JIS で正しく出た。** XP-C58K の印字結果を目視確認済み
   （漢字・カタカナ・バーコード・QR・カット）。`FS C 1` ＋ `FS &` ＋ Shift-JIS バイト列が
   この機種の正解。ログからは「バイト列を受け付けた」しか分からないので、これは目視だけが根拠。

6. **レシート 1 枚は 1 転送で送る。** プリンタは 1 行たまった時点で印字を始めるため、分割するとホスト側の遅延がそのまま印字のガタつきになる。

## 非対象

- **IPP / PWG-Raster / PostScript / PCL**: レーザ・インクジェット向けで、実機も無い。Printer クラスの上に載る別の言語であり、`vendorWrite()` で送る点は同じなので必要なら同じ層構成で足せる
- **1284.4 / IEEE 1284 パケットモード**（protocol `0x03`）: 実機が無い
- **双方向のステータス自動監視**: ライブラリ側でポーリングタスクを持つことはしない。ステータスをいつ読むかはアプリの判断
- **UTF-8 → Shift-JIS 全変換表**: ~7000 エントリを example に持たせない。ラスタ画像経路を用意して代替する
- **ハブ経由の複数プリンタ**: 単体で確認済み。同時接続は未検証

## テスト計画

| テスト | 内容 | 用紙 |
|---|---|---|
| `tests/unit/escpos` | クラス要求のバイト列、デバイス ID の解析（空・短絡・切り詰め）、ポートステータスのビット（`0x00` 含む）、全 ESC/POS コマンドのバイト列、ビルダのオーバーフロー、レシートの漢字モード対称性 | — |
| `tests/manual/printer_escpos` | 実機のクラス要求とリアルタイムステータス、20 回連続ポーリング、`SOFT_RESET` 後の生存確認 | **使わない** |
| `tests/manual/printer_print` | レシート 1 枚を 1 転送で印字＋カット、印字前後のステータス、転送後のステータス経路生存 | 1 枚/回 |
| `tests/probe/printer_class` | クラス要求のアドレッシング総当たり（探索用、pass/fail ではなくログが成果） | 使わない |

印字結果（日本語が出ているか、バーコード・QR が読めるか、カットが綺麗か）はログでは分からないので、`printer_print.py` の docstring にチェックリストを置いて目視確認に回している。

## 進行状況

- [x] descriptor 実測とカテゴリ決定
- [x] `className()` に `0x07` / `0x0b`
- [x] `PrinterProtocol.hpp` / `EscPos.hpp` / `PrinterDevice.hpp` / `ReceiptJa.hpp` / `.ino`
- [x] `tests/unit/escpos`
- [x] `tests/probe/printer_class`（クラス要求の切り分け）
- [x] `tests/manual/printer_escpos`（用紙不要）
- [x] `tests/manual/printer_print`（1 枚印字＋カット）
- [x] example README（en/ja）、本体 README のクラス対応表、CHANGELOG

## 残るリスク

- **日本語フォント ROM の確認は目視のみ**（XP-C58K では確認済み）。ログからは「バイト列を受け付けた」しか分からないので、他機種では文字化けする可能性がある。ただし文字化けしても伝票の他の要素（バーコード・QR・カット）は影響を受けない
- **紙幅の前提**が `receipt::COLUMNS = 32`（58mm・フォント A）。80mm 機は 48 で、レイアウトは差し替えが必要
- **単方向プリンタ**（protocol `0x01`）は実機が無い。`realtimeStatus()` が false を返し `GET_PORT_STATUS` だけになる経路は、コード上は通るが未検証
- **カッターのジャム系エラー**は意図的に起こしていない。`DLE EOT 3` の bit 3 で検出する実装だが、実際にジャムさせた確認はしていない
