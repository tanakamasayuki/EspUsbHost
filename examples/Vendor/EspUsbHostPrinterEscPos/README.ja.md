# EspUsbHostPrinterEscPos

> English: [README.md](README.md)

USB レシートプリンタで印字します。ESC/POS の日本語テキスト、バーコード、QR コード、
オートカッターまで。Xprinter XP-C58K（`0483:070b`）で実機確認済みです。

> **状態: 動作確認済み（ESP32-S3）。** 以下のバイト列は
> `tests/manual/printer_escpos`（用紙を消費しない）と
> `tests/manual/printer_print`（1 枚印字）で実機確認しました。ログは
> [実測結果](#実測結果) にあります。

| ファイル | 内容 |
|---|---|
| `PrinterProtocol.hpp` | USB Printer クラス: クラス要求 3 種、`GET_PORT_STATUS` のビット、IEEE 1284 デバイス ID の形式とフィールド検索。Arduino / USB 非依存 |
| `EscPos.hpp` | ESC/POS 印字データ言語: 範囲チェック付きコマンドビルダとリアルタイムステータスの応答。Arduino / USB 非依存 |
| `PrinterDevice.hpp` | プリンタインタフェースの探索、ベンダバルク API での claim、EP0 のクラス要求、バルクでの印字データ送出とステータス読み取り。レイアウトも VID/PID も持たない |
| `ReceiptJa.hpp` | 内容: 日本語レシートを Shift-JIS バイト列で保持。ASCII のみのフォールバック伝票も。レイアウトや言語を変えるならこのファイルを差し替える |
| `EspUsbHostPrinterEscPos.ino` | 接続してデバイス ID と用紙状態を表示し、任意でレシートを 1 枚印字 |

## なぜ `examples/Vendor/` なのか

**プリンタのインタフェースクラスは `0x07` で、ベンダ固有の `0xff` ではありません。**

`examples/` のディレクトリは「デバイスの USB クラス」ではなく **「そのサンプルがライブラリの
どの API を使うか」** で分けています。`Vendor/` は「ベンダバルク/コントロール API の上に
作ってある」という意味で、まさにこのサンプルがそれです（ライブラリ自体にプリンタ対応は
ありません）。隣の `EspUsbHostUsbtmcScpi` はクラス `0xfe` ですが同じ理由でここにいます。

クラス名のディレクトリ（`Ccid/`、`UsbNetwork/`、`Storage/`、`Audio/`）は、ライブラリに
そのクラス専用 API がある場合だけ存在します。

## 用紙について

印字は用紙を消費するので、スケッチでは両方とも既定で OFF です。

```cpp
static constexpr bool PRINT_SAMPLE = false;  // レシートを 1 枚印字
static constexpr bool CUT_PAPER = false;     // 印字後にカット
```

出荷状態ではデバイス ID と用紙状態を読むだけで、用紙は動きません。印字前には
`PrinterDevice::checkPaper()` を呼び、用紙切れやエラーなら印字を拒否します。この確認が
重要なのは、**用紙切れのプリンタに送った印字データはプリンタ内にバッファされ**、用紙を
交換した後に次の印字物と混ざって出てくるからです。

## しくみ

このクラスは印字データのストリームを運ぶだけで、中身については何も規定しません。
つまり層は 2 つに分かれます。バルク 2 本と EP0 要求 3 種という USB 層と、ペイロードで
ある ESC/POS です。

### ライブラリ側

**ライブラリへの追加は不要でした。** USB 側はこれで全部です。

```cpp
// インタフェース番号を明示するので、ベンダ固有でないクラス 0x07 でも claim できる。
// 印字データは一方向でステータスは要求時のみ読むので READ_ON_DEMAND が正しい
// （継続 IN 転送だと NAK を返し続けるだけになる）。
usb.vendorOpen(address, interfaceNumber, ESP_USB_HOST_VENDOR_READ_ON_DEMAND);
usb.vendorSetAutoZlp(true, address);   // 全 OUT を短パケットで終端する
usb.vendorWrite(receipt, length, address);
usb.vendorReadSync(reply, sizeof(reply), &received, timeoutMs, address);
usb.vendorControlTransfer(0xa1, printer::REQ_GET_DEVICE_ID, 0, wIndex,
                          data, sizeof(data), &received, address);
```

レシート 1 枚は **1 回の** `vendorWrite()` で送ります。プリンタは 1 行たまった時点で
印字を始めるので、ホスト側の遅延をはらむ複数転送に分けると印字がガタつき、機種によっては
タイムアウトで行が途中まで出てしまいます。

### クラス要求 3 種

| 要求 | bmRequestType | bRequest | 備考 |
|---|---|---|---|
| GET_DEVICE_ID | `0xa1` | 0x00 | wValue = コンフィグレーションインデックス、**wIndex = (interface << 8) \| alternate setting** |
| GET_PORT_STATUS | `0xa1` | 0x01 | 1 バイト。wIndex = インタフェース番号 |
| SOFT_RESET | `0x21` | 0x02 | データステージなし。プリンタのバッファをフラッシュ |

`GET_DEVICE_ID` だけが例外で、wIndex のバイト順が他と逆です。だから呼び出し側で組み立てず
`printer::deviceIdIndex()` に持たせています。

デバイス ID 自体は **ビッグエンディアン 2 バイトの長さ（自身を含む）** の後に
`KEY:value;` が並ぶ形式です。プリンタはキーの長短どちらの綴りも使う（`MFG` か
`MANUFACTURER`、`MDL` か `MODEL`、`CMD` か `COMMAND SET`）ので、両方試す価値があります。

### GET_PORT_STATUS と、`0x00` を「回答なし」として扱う理由

ビットは旧来の Centronics ステータス線で、うち 2 つは論理が反転しています。

| bit | 意味 |
|---|---|
| 5 | PaperEmpty — **用紙が無いときに 1** |
| 4 | Select — 選択されているときに 1 |
| 3 | NotError — **エラーが無いときに 1** |

文字どおり読むと `0x00` は「非選択・エラー・用紙あり」です。EP0 に応答して問題なく印字して
いるプリンタがその状態にあるはずはありません。そして XP-C58K が返すのはまさに `0x00` で、
他のやり取りの前後でも `SOFT_RESET` の後でも毎回同じ、その間リアルタイムステータスは
ずっと「正常」と答えています。

そこで `decodePortStatus()` は `0x00` をデコード結果ではなく `unknown` として報告します。
文字どおり読むと健全なプリンタで印字を拒否してしまうからです。代償は「本当に非選択＋
エラーでちょうど `0x00` を返す状態」が unknown に見えることですが、こちら側に倒すのが安全
です。呼び出し側は実際に答えが返るリアルタイムステータスにフォールバックできます。

### リアルタイムステータス `DLE EOT n`

**実際に使えるステータス経路はこちら**で、これは USB ではなく ESC/POS 側の機能です。
プリンタは印字バッファより先にこれを処理するので、印字中でもオフラインでも答えます。
それがこのコマンドの存在理由です。

| n | 内容 |
|---|---|
| 1 | プリンタステータス（bit 3 = オフライン） |
| 2 | オフライン要因（カバー開、紙送りボタン） |
| 3 | エラーステータス（bit 3 = エラー状態: カッタージャム、カバー開） |
| 4 | 用紙センサ（bit 2,3 = ニアエンド、bit 5,6 = 用紙切れ） |

応答は必ず bit 0 が 0、bit 1 が 1 に固定されます。この 2 ビットの確認が、ステータスバイトと
「混乱したデバイスがエコーした印字データ」を見分ける方法で、実機テストでも確認しています。

`PrinterDevice::checkPaper()` はこの経路を優先し、失敗したら `GET_PORT_STATUS` に
フォールバックします。単方向プリンタ（protocol `0x01`、バルク IN なし）でも取れる範囲の
答えは返ります。

### 日本語テキスト

日本語 ESC/POS プリンタは 2 バイト文字を自分のフォント ROM から **Shift-JIS**（または
`FS C n` で選ぶ JIS）としてデコードします。UTF-8 のことは何も知りません。

```cpp
out.kanjiCode(escpos::KANJI_CODE_SHIFT_JIS);  // FS C 1
out.kanjiOn();                                // FS &
out.bytes(sjisBytes, sizeof(sjisBytes));
out.kanjiOff();                               // FS .
```

`ReceiptJa.hpp` はサンプル文をコメントに元テキストを添えた Shift-JIS バイト配列として
持っています。文字列リテラルでなく配列にしているのは意図的で、Shift-JIS の 2 バイト目は
ASCII の 16 進数字になり得るため、`"\x82"` の直後にそういう文字が来ると C では 2 バイトでは
なく **1 つの `\x` エスケープ**として解釈されてしまいます。

ASCII 行に戻るときは漢字モードを解除しなければなりません。ONのままだと `0x81..0x9f` の
バイトが 2 バイト文字の 1 バイト目と解釈され、次のバイトを飲み込みます。ホスト単体テストで
`FS &` と `FS .` の数が一致することを検査しています。

ビルド時に決まらないテキストを出すなら、必要な範囲だけ UTF-8 → Shift-JIS の表を持つか、
ビットマップに描いて `raster()` で送ります（後者はフォント ROM に無い文字にも使えます）。

### ビルダが出すコマンド

| 用途 | コマンド |
|---|---|
| 初期化 | `ESC @` |
| コードページ / 漢字コード系 | `ESC t n` / `FS C n` |
| 漢字モード | `FS &` / `FS .` |
| 寄せ・強調・下線・白黒反転 | `ESC a n`、`ESC E n`、`ESC - n`、`GS B n` |
| 文字サイズ | `GS ! n`（`(幅-1) << 4 \| (高さ-1)`、各 1..8） |
| 紙送り | `LF`、`ESC d n` |
| カット | `GS V m [n]` — **66 は先に紙送りする**。レシートではこれが必要で、送らないと最後の行が刃の位置に来て切られてしまう |
| バーコード | `GS k m n d1..dn`（長さ前置形式なので任意のバイトを含められる） |
| QR コード | `GS ( k` — モデル・モジュールサイズ・誤り訂正・格納・印字の 5 コマンド |
| ラスタ画像 | `GS v 0 m xL xH yL yH ...` — 1bpp、MSB first、行はバイト境界にパディング |
| リアルタイムステータス | `DLE EOT n` |

`escpos::Builder` は範囲チェック付きで、黙って切り詰めるのではなくオーバーフローフラグを
立てます。`PrinterDevice::write()` はオーバーフローしたビルダを拒否します。これは見た目より
重要で、途中で切れたレシートは末尾（カットコマンドや、直前コマンドの引数）を欠いており、
引数を待つプリンタは **次の**レシートの先頭を食べてしまいます。

## 実測結果

`tests/manual/printer_escpos`（用紙を消費しない）、ESP32-S3:

```
printer address=2 interface=0 protocol=0x02 bidirectional=1 bulk_out=0x01 bulk_in=0x82 mps=64
device id raw 2 bytes: 00 02
device id ""
device id is empty - the request works, the printer has no ID string
port status 0x00 unknown=1 paper_empty=0 selected=0 error=0
port status carries no information on this printer
DLE EOT 1 0x16
DLE EOT 2 0x12
DLE EOT 3 0x12
DLE EOT 4 0x12
paper near_end=0 out=0
checkPaper out=0 near_end=0 error=0
repeated polling 20/20 answered
SOFT_RESET ok
port status after reset 0x00 unknown=1
DLE EOT 4 after reset 0x12
```

`tests/manual/printer_print`（1 枚印字＋カット）:

```
status before: printer=0x16 offline=0 error=0x12 error_state=0 paper=0x12 near_end=0 out=0
receipt 586 bytes, cut=1
receipt sent
status after: printer=0x16 offline=0 error=0x12 error_state=0 paper=0x12 near_end=0 out=0
status after printing 5/5 answered
```

そして伝票の目視確認: 日本語のタイトル、CODE128 バーコード、QR コード、綺麗なカット。
つまりこのプリンタのフォント ROM は Shift-JIS で、`FS C 1` ＋ `FS &` ＋ Shift-JIS バイト列の
組み合わせが正解でした。

ディスクリプタ（`tests/manual/device_dump`）:

```
VID:PID 0483:070b class=0x00(per-interface)
Strings manufacturer="Xprinter " product="" serial=""
  Interface 0 alt=0 class=0x07(Printer) subclass=0x01 protocol=0x02 endpoints=2
    Endpoint iface=0 ep=0x01 dir=OUT type=bulk max_packet=64
    Endpoint iface=0 ep=0x82 dir=IN  type=bulk max_packet=64
```

列挙時に `ENUM: Device returned less bytes than requested` が 2 回出ます。プリンタが
product と serial の文字列ディスクリプタを申告しておいて短く返すためです。無害で（列挙は
続行し全機能が動く）、シリアル監査が新規扱いしないよう `tests/conftest.py` に既知事象として
登録しています。

### このプリンタから分かったこと

1. **クラス要求は任意実装で、この機種はどちらも実質未実装。** `GET_DEVICE_ID` は
   形式は正しい **空の** ID（`00 02`）を返し、`GET_PORT_STATUS` は常に `0x00` を返します。
   どちらも STALL せず「応答はする」ので、ホストからは「未実装」と「報告する事が無い」を
   返ってきた中身以外では区別できません。このサンプルの初版は両方を失敗として扱って
   いましたが、`tests/probe/printer_class` でアドレッシングの候補を総当たりした結果、
   要求自体は正常で、プリンタに言うことが無いだけだと分かりました。
2. **頼るべきステータス経路はリアルタイムステータス。** ポーリング 20/20 応答、
   586 バイトの印字転送直後でも 5/5 応答でした。
3. **この機種では Shift-JIS 経路が正解。** 漢字もカタカナも意図した文字で出ました。
   `FS C 1`（Shift-JIS）＋ `FS &` ＋ Shift-JIS バイト列が、このフォント ROM の求めるものです。
   これはログからは読み取れない唯一の結果です。
4. **`SOFT_RESET` はこの機種では安全。** 実行後も EP0 もバルクも動きました。確認する
   価値はあります（USBTMC のサンプルではデータトグルがずれるため
   `CLEAR_FEATURE(ENDPOINT_HALT)` を外す必要がありました）。

## 他機種へ移すとき

- **レイアウトや言語を変える**: `ReceiptJa.hpp` を差し替え。他のファイルはレシートが
  何かを知りません。
- **別のプリンタ**: `PrinterProtocol.hpp` / `EscPos.hpp` / `PrinterDevice.hpp` に VID/PID も
  機種前提もありません。確認すべきは 2 点、2 バイトフォント ROM を持つか
  （無ければ `PRINT_JAPANESE = false` で ASCII 伝票を印字）、そして紙幅です。
  `receipt::COLUMNS` は 32（フォント A で 58mm 相当）で、80mm 機なら 48 です。
- **単方向プリンタ**（protocol `0x01`）: 印字はできますがバルク IN が無いので
  `realtimeStatus()` は false を返し、`GET_PORT_STATUS` だけが残ります。

## 参考資料

- USB-IF, *Universal Serial Bus Device Class Definition for Printing Devices*, version 1.1
- IEEE 1284-2000, デバイス ID 文字列形式
- Epson, *ESC/POS Command Reference*（`ESC` / `GS` / `FS` / `DLE EOT` 系コマンドの公開資料）

これらの公開仕様から実装しました。GPL ライセンスのプリンタドライバは参照していません。

> Xprinter は各権利者の商標、Epson および ESC/POS はセイコーエプソン株式会社の商標です。
> 本プロジェクトはいずれとも関係なく、承認・認定も受けていません。
