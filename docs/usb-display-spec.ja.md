# EspUsbHost USB Display (DL-1xx) 対応 仕様案

## 目的

USB グラフィックスディスプレイアダプタを EspUsbHost から使えるようにする。

対象チップは DL-1xx 系（DL-120 / DL-160 / DL-115 / DL-125 / DL-165 / DL-195）に限定する。出力は LovyanGFX の panel として提供し、描画面は兄弟ライブラリ [LGFXVirtualCanvas](https://github.com/tanakamasayuki/LGFXVirtualCanvas) を被せて使う想定とする。

ゴールは **ESP32-S3（Full-speed OTG）から 1920x1080 を表示できること**。フレームレートは追わない。静止 UI が出て、変化した部分が現実的な時間で追従すれば達成とする。

DL-1xx のプロトコル処理そのものはライブラリ本体に入れない。本体には「非同期 bulk OUT」など汎用的に使える API だけを追加し、DL-1xx 固有の処理は `examples/` 側に置く。この分割方針は `vendor-api-spec.ja.md` の vendor bulk/control API の延長線上にある。

## 現在の調査結果

### ライセンス

| 参照元 | ライセンス | 扱い |
|---|---|---|
| Florian Echtler のリバースエンジニアリング仕様 | 公開ドキュメント | 参照する（一次情報） |
| OpenBSD `sys/dev/usb/udl.c` | ISC | 参照する（DL-120/125/160/165/195 対応） |
| htlabnet/Pico_USB_Disp `docs/protocol.md` | MIT | 参照する（DL-165 実機検証済みの記述を含む） |
| Linux `drivers/video/fbdev/udlfb.c` | GPL-2.0 | **参照しない** |
| `libdlo` | LGPL-2.1 | **参照しない** |
| DisplayLink Embedded SDK (Synaptics) | 商用 / NDA | **参照しない** |

本ライブラリは MIT（`LICENSE`）。GPL/LGPL ドライバと商用 SDK のコードは一切取り込まず、permissive ライセンスの実装と公開仕様のみを参照してスクラッチ実装する。example の README にこの由来を明記する。

### 命名と商標

「DisplayLink」は DisplayLink Corp（現 Synaptics）の登録商標である。指名的使用（対応ハードウェアの特定）は行うが、機能名・API 名としては使わない。

- コード側の識別子・ファイル名・ディレクトリ名・README の対応表の機能名欄は、型番の `DL-1xx` / `Dl1xx` を使う
- 散文で対応ハードを特定する箇所でのみ「DisplayLink DL-1xx 系チップ搭載アダプタ」と書く
- example README に商標表記を置く: `DisplayLink is a trademark of Synaptics Incorporated. This project is not affiliated with, endorsed by, or certified by Synaptics.`
- ロゴは使わない。「対応」「認定」など認証を示唆する表現は使わない

前例として Linux は `udlfb`、OpenBSD は `udl`、Pico_USB_Disp は `usb_disp_prot_dl-1xx.cpp` と、いずれもコード側にブランド名を使っていない。

### DL-1xx プロトコル

DL-1x0（"Alex"）と DL-1x5（"Ollie"）は同一プロトコル。VID `0x17E9`。

USB 構成（手元の DL-165 実機で確認済み、`tests/manual/vendor_bulk_out_only` の出力より）:

- VID:PID `17e9:0360`、manufacturer `DisplayLink`、product `USB to DVI-17`
- vendor class (0xFF) インタフェース 1 本、endpoint 3 本
  - bulk OUT `0x01` MPS 64: コマンド／ピクセルストリーム
  - interrupt IN `0x82` MPS 8 interval 4: 本実装では使わない
  - bulk OUT `0x0a` MPS 64: **2 本目の bulk OUT**。用途不明で本実装では使わない
- bus-powered、max_power 500mA
- Full-speed にフォールバックしても同じ構成が出る（ESP32-S3 で使える根拠）。ESP32-S3 のホストポートでは常に Full-speed になる

bulk OUT が 2 本あるため、endpoint の選択規則が問題になる。`vendorOpen()` は descriptor 順で最初の bulk OUT（`0x01`）を選ぶ。

control request:

| bmRequestType | bRequest | wValue | wIndex | データ | 用途 |
|---|---|---|---|---|---|
| 0x40 | 0x12 | 0 | 0 | 16 バイトのキー | チャネル選択（標準キーで暗号化無効） |
| 0xC0 | 0x02 | i << 8 | 0xA1 | 2 バイト IN | EDID 読み出し（1 バイトずつ、`buf[1]` が値） |

標準チャネルキー: `57 CD DC A7 1C 88 5E 15 60 FE C6 97 16 3D 47 F2`

bulk コマンド（すべて `0xAF` 始まり）:

| コマンド | 長さ | 意味 |
|---|---|---|
| `AF 20 reg val` | 4 | レジスタ書き込み |
| `AF 6B addr[3] count data...` | 可変 | RLE 圧縮ピクセル書き込み（base16 プレーン、RGB565） |
| `AF 60 addr[3] count data...` | 可変 | 無圧縮書き込み（base8 プレーン、24bpp 時のみ） |
| `AF 6A dst[3] count src[3]` | 9 | 画面内矩形コピー（base16） |
| `AF 62 dst[3] count src[3]` | 9 | 画面内矩形コピー（base8、24bpp 時のみ） |
| `AF A0` | 2 | flush（バッファ済みコマンドの実行を強制） |
| `AF` 連続 | - | パディング（no-op） |

アドレスはすべてデバイスフレームバッファへのバイトアドレス。`count` は 256 を `0` で表す。

ビデオレジスタはレジスタ `0xFF` によるロックで囲む: `0xFF <- 0x00`（ロック）→ 各レジスタ設定 → `0xFF <- 0xFF`（アンロック＝適用）。タイミング値は生の数値ではなく 16bit LFSR カウント（tap 15, 4, 2, 1、初期値 0xFFFF から N 回ステップした値）。

| レジスタ | エンコード | 内容 |
|---|---|---|
| 0x00 | 生 | 色深度（0 = 16bpp、1 = 24bpp） |
| 0x01 / 0x03 | LFSR16 | 水平表示開始 / 終了（sync 開始から xds = HBP + HSYNC） |
| 0x05 / 0x07 | LFSR16 | 垂直表示開始 / 終了 |
| 0x09 | LFSR16 | 水平トータル - 1 |
| 0x0B | LFSR16(1) | 水平 sync 開始 |
| 0x0D | LFSR16 | 水平 sync 終了（HSYNC + 1） |
| 0x0F | 生 BE | 水平ピクセル数 |
| 0x11 | LFSR16 | 垂直トータル |
| 0x13 | LFSR16(0) | 垂直 sync 開始 |
| 0x15 | LFSR16 | 垂直 sync 終了（VSYNC） |
| 0x17 | 生 BE | 垂直ライン数 |
| 0x1B | 生 LE | ピクセルクロック / 5kHz |
| 0x1F | 生 | ブランキング（0x00 = 表示 ON） |
| 0x20-0x22 | 24bit | base16 プレーン開始アドレス（0） |
| 0x26-0x28 | 24bit | base8 プレーン開始アドレス（width * height * 2） |

`AF 6B` の RLE は、raw ラン（`raw_cnt` 個のリテラルピクセル）と repeat ラン（直前ピクセルの追加繰り返し回数）を交互に並べる。ピクセルは RGB565 のビッグエンディアン。1 コマンドあたり最大 256 ピクセル。単色 256 ピクセルは 10 バイト、最悪ケース（全ピクセル異なる）は 519 バイトで、生の 512 バイトからわずかに膨張する。

表示保持の挙動（DL-165 実機で確認されている内容）:

- モード設定と描画のあと bulk 転送を完全に止めても、チップは内部フレームバッファから走査を続けて表示を保持する。キープアライブ不要
- ただしモニタ側の HPD 相当イベント（モニタ抜き差し、キャプチャ機器のクローズ）で出力が落ち、黒画面のまま戻らない。ピクセル書き込みでは復帰せず、モードレジスタ列の再送で即座に復帰する

### Full HD の実現性（数値根拠）

プロトコル上の上限をすべて確認した結果、1920x1080 / 16bpp は成立する。

| 項目 | 値 | 判定 |
|---|---|---|
| `AF 6B addr[3]` = 24bit バイトアドレス | 上限 16 MB | OK |
| 1920x1080x2 の必要フレームバッファ | 4,147,200 B = 0x3F4800 | OK（24bpp のデュアルプレーンでも 0x5EEC00 で収まる） |
| レジスタ 0x1B（ピクセルクロック / 5kHz、16bit） | 148.5 MHz → 29700 | OK（16bit で 327 MHz 相当まで表現可） |
| DL-1x5 の内蔵 DRAM | 16 MB | OK |
| DL-165 の最大解像度 | ファミリ上限 2048x1152、製品実装は 1920x1080 / 1600x1200 | OK（手元の DL-165 + Full HD モニタで解像度的に問題ないことを確認済み） |
| DL-120 / DL-160 の最大解像度 | 1600x1200 / 1680x1050 | Full HD 不可。低解像度の検証用に使う |

Full-speed bulk OUT の実効は **1.098 MB/s** と確定した（Phase 2 の `tests/manual/vendor_bulk_throughput`、ESP32-S3 + DL-165 実機。Full-speed bulk の理論上限 1.216 MB/s の約 90%）。これを基準にした転送量の見積り:

| ケース | 転送量 | 時間 |
|---|---|---|
| 全画面単色（RLE 256px = 10B） | 約 81 KB | 0.07 s |
| 一般的な UI 全画面（RLE で 5〜20 倍圧縮） | 0.2〜0.8 MB | 0.2〜0.7 s |
| 全画面写真・ノイズ（519B / 256px） | 4.2 MB | 約 3.8 s |
| 差分転送で 1〜2 タイルのみ更新 | 10〜20 KB | 0.02 s |

圧縮率だけが未実測の変数として残る。これは Phase 6 の `usb_display_throughput` で描画パターン別に確定させる。

### LGFXVirtualCanvas との組み合わせ

LGFXVirtualCanvas 1.2.0 以降を前提とする（差分転送 `setDiffMode(LGFXVirtualDiffMode::Tile)` が 1.2.0 で追加された）。

- 垂直タイル分割なので **ホスト側にフルフレームバッファを持たない**。Full HD / 16bpp・既定 19KB 予算で 5 行タイル（1920 x 5 x 2 = 19,200 B）x 216 タイル。複数タイル時はダブルバッファが自動で有効になるため約 38 KB。PSRAM なしの ESP32-S3 でも成立する
- タイルは画面幅いっぱいの帯なので、デバイスフレームバッファ上では**完全に連続したアドレス領域**になる。`AF 6B` は連続バイトアドレスなので行境界をまたいで RLE ランを継続でき、コマンドヘッダのオーバーヘッドが最小になる。任意矩形の部分更新よりも効率が良い
- 差分転送は DL-1xx の「無通信でも表示保持」と噛み合う。転送を省略したタイルは本当に何も送らなくてよい
- ダブルバッファの切り替えは 1.2.0 で「タイルごと」から「転送ごと」に変更されている。本 panel は RLE エンコード時に必ずタイルバッファから USB DMA バッファへコピーするため、`writeImage()` から戻った時点でタイルバッファは再利用可能で、この不変条件は自然に満たされる。**タイルバッファをそのまま DMA に渡すゼロコピー経路は作らない**
- `invalidate()` の契約: 再確保・設定変更・panel の回転/サイズ/色深度変化は自動で無効化される。USB 再接続と HPD 復帰のためのモードレジスタ再送は自動検出されないので、panel 側に世代カウンタと ready コールバックを持たせ、example から `screen.invalidate()` を呼ぶ

### 本体 API の不足点

1. ~~`vendorOpen()` が bulk IN / OUT のペアを必須にしている~~ → Phase 1 で対応済み。DL-1xx は bulk OUT + interrupt IN で bulk IN を持たないため open できなかった。あわせて、同一 interface に複数の bulk OUT があるとき descriptor 順で最後の endpoint を選んでしまう問題も修正した（手元の DL-165 は bulk OUT を 2 本持つため、`0x0a` が選ばれていた）
2. ~~`vendorWrite()` が完全同期~~ → Phase 2 で対応済み。1 転送ずつのストア&フォワードのため、小さい転送で Full-speed の帯域が埋まらなかった（512 byte 転送で上限の 80%）
3. ~~bulk OUT のパケット境界（ZLP）処理がユーザー側にある~~ → Phase 2 で auto ZLP をライブラリ責務にし、ADB example から手書き処理を削除
4. ~~転送統計がない~~ → Phase 2 で `vendorWriteStats()` を追加

control transfer は追加不要。DL-1xx のチャネルキー送信は `vendorControlOut(0x12, 0, 0, key, 16)`、EDID 読み出しは `vendorControlIn(0x02, i << 8, 0xA1, buf, 2)` で既存 API のまま通る。

ESP-IDF 側の裏付け（`usb_host.h` / `usb_types_stack.h`）:

- bulk OUT は MPS 超でも自動で MPS サイズのパケット列 + short packet に分割される。8〜16 KB の 1 転送でよい
- 転送オブジェクトは無制限に再利用可能で、プール前提の設計になっている
- 同一 endpoint に複数転送をキューできる
- 完了コールバックは `usb_host_client_handle_events()` の文脈、つまり既存の client task 上で呼ばれる
- bulk/interrupt OUT で転送長が MPS の倍数になった場合、ZLP はホストが別途送る必要がある

## 非目的

- DL-1xx 以外のチップ（MacroSilicon MS912x / MS913x、MCT Trigger 6 など）。これらは High-speed 必須で、制御チャネルも HID class request や別プロトコルになる
- 24bpp デュアルプレーン（初期実装は 16bpp のみ）
- 複数ディスプレイの同時出力
- EDID の優先タイミングからのモード自動生成（初期はテーブル + EDID は情報表示のみ）
- 画面内矩形コピー（`AF 6A`）の LovyanGFX `copyRect` への接続（Phase 5 以降の任意項目）
- DL-1xx 処理のライブラリ本体への取り込み
- 動画再生に足るフレームレート

## 本体に追加する汎用 API 案

DL-1xx 専用にはせず、vendor bulk OUT 全般で使える形にする。既存の `vendor*` API の拡張として入れる。

### `vendorOpen()` の緩和

bulk OUT のみのインタフェースを許可する。

- bulk IN と bulk OUT の両方があるインタフェースを優先する（現状のまま）
- ペアが見つからない場合、bulk OUT のみのインタフェースを採用する
- bulk IN がある場合のみ継続 IN 転送を張る。bulk IN がない場合は張らない
- `endpointChannelCount` の加算を固定 +2 ではなく実際に開いた endpoint 数にする
- bulk IN のみのインタフェースは引き続き失敗扱いとする（用途がなく、`vendorWrite` が使えない）

API シグネチャは変えない。後方互換。`vendor-api-spec.ja.md` の「片方向 endpoint しかない interface は初期実装では失敗扱いにする」を更新する。

### 非同期 bulk OUT キュー

```cpp
struct EspUsbHostVendorWriteStats
{
  uint32_t submitted = 0;       // submit 成功回数
  uint32_t completed = 0;       // 完了コールバック到達回数
  uint32_t errors = 0;          // 完了ステータス異常の回数
  uint32_t queueFullEvents = 0; // acquire が空きなしで待たされた回数
  uint64_t bytes = 0;           // 完了した転送の累計バイト数
  uint32_t zlp = 0;             // 送出した ZLP の回数
};

bool     vendorWriteQueueBegin(size_t depth, size_t bufferBytes,
                               uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void     vendorWriteQueueEnd(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool     vendorWriteQueueReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

// ゼロコピー経路: プールの DMA バッファを借りて直接書き込む
uint8_t *vendorWriteAcquire(size_t *capacity, uint32_t timeoutMs = 0,
                            uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool     vendorWriteSubmit(uint8_t *buffer, size_t length,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void     vendorWriteRelease(uint8_t *buffer, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

// コピー経路: 既存 vendorWrite と同じ使い勝手のノンブロッキング版
bool     vendorWriteAsync(const uint8_t *data, size_t length,
                          uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

size_t   vendorWritePending(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t   vendorWriteQueueFree(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool     vendorWriteFlush(uint32_t timeoutMs, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

EspUsbHostVendorWriteStats vendorWriteStats(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
void     vendorWriteStatsReset(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
```

内部実装は既存の audio 出力（`audioOutTransfers[ESP_USB_HOST_AUDIO_OUTPUT_TRANSFERS]`、[`EspUsbHost.h:1387`](../src/EspUsbHost.h#L1387)）の事前確保プール + コールバック補充と同型にする。

挙動:

- `vendorWriteQueueBegin()` は `depth` 個の `usb_transfer_t` を各 `bufferBytes` で事前確保する。`depth` の上限は `ESP_USB_HOST_VENDOR_WRITE_QUEUE_MAX_DEPTH`（初期値 8）。既に確保済みで同じパラメータなら成功扱い、異なるパラメータなら失敗（先に `vendorWriteQueueEnd()`）
- `vendorWriteAcquire()` は空きスロットのバッファ先頭ポインタを返し、`capacity` に `bufferBytes` を入れる。空きがなければ `timeoutMs` まで待ち、それでも空かなければ `nullptr`。`timeoutMs = 0` は即時リターン。待った場合 `queueFullEvents` を加算する
- `vendorWriteSubmit()` は `vendorWriteAcquire()` で得たポインタと実長を渡して submit する。返値は submit の成否のみで、転送完了は待たない。完了状況は `vendorWriteStats()` で見る
- `vendorWriteRelease()` は acquire したが送らないことにした場合にスロットを返す
- `vendorWriteAsync()` は内部で acquire + memcpy + submit を行う。`length > bufferBytes` の場合は分割せず失敗にする（呼び出し側が転送サイズを意識すべき用途のため）
- キュー未確保状態で `vendorWriteAsync()` / `vendorWriteAcquire()` を呼んだ場合は失敗。暗黙の確保はしない
- 完了コールバックはブロックしないので、これらの API は **USB client task 上のコールバックからも呼べる**。同期版 `vendorWrite()` の制約は残したまま、非同期版だけこの制約を外す
- `vendorWriteFlush()` は in-flight がゼロになるまで待つ。timeout 時は false で、in-flight はそのまま
- 転送エラーは `errors` の加算とログのみで、キューは止めない。halt した場合のみ endpoint の halt/flush/clear を行い、以降の submit を失敗させる
- device 切断時は in-flight の完了を待ってからプールを解放する（同期版の abandoned 処理と同じ考え方）

`onVendorWriteComplete()` コールバックは初期範囲に含めない。統計とキュー空き数で足りるかを実測で判断する。

### ZLP / パケット境界

```cpp
uint16_t vendorOutPacketSize(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool     vendorWriteZlp(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void     vendorSetAutoZlp(bool enable, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool     vendorAutoZlp(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
```

- `vendorOutPacketSize()` は open 済み bulk OUT の MPS を返す。未 open は 0
- `vendorWriteZlp()` はゼロ長 bulk OUT を送る。非同期キューが有効ならキュー経由、無効なら同期送信
- `vendorSetAutoZlp(true)` にすると、転送長が MPS の倍数（かつ 0 でない）のとき ZLP を自動で続けて送る。既定は off で、既存の挙動は変わらない
- DL-1xx は ZLP を必要としないため example では off のままにする。この API は ADB と CDC 系のために入れる

## example 構成

`examples/Vendor/EspUsbHostDisplayDl1xx/` に置く。DL-1xx は vendor class の bulk デバイスなので `Vendor/` 配下が素直。他チップ系を追加する段になったら `examples/Display/` の新設を検討する。

`.ino` はユーザーコードのみにし、ベンダー処理はスケッチ同梱ヘッダに分離する。すべて header-only（inline）で、他プロジェクトへコピーして再利用できる形にする。

```
examples/Vendor/EspUsbHostDisplayDl1xx/
  EspUsbHostDisplayDl1xx.ino   ユーザーコードのみ
  Dl1xxProtocol.hpp            0xAF コマンド生成・レジスタ・LFSR16・RLE エンコーダ（USB 非依存の純粋関数）
  Dl1xxModes.hpp               タイミングテーブル
  Dl1xxDevice.hpp              EspUsbHost 層: 検出 / claim / チャネルキー / EDID / vendor descriptor / モード設定 / flush
  Panel_Dl1xx.hpp              lgfx::Panel_Device 派生 + LGFX_Dl1xx
  README.md / README.ja.md
  sketch.yaml
```

層分割の意図:

- `Dl1xxProtocol.hpp` は USB と LovyanGFX の両方に依存しない純粋関数の集まりにする。これにより LFSR16 と RLE エンコーダをホスト上の g++ で単体テストできる（`tests/unit/keymap` と同じ手法）。エンコーダのバグは実機では画面の乱れとしてしか観測できないため、ここを切り離す価値が大きい
- `Dl1xxDevice.hpp` が EspUsbHost の vendor API を呼ぶ唯一の層。RLE エンコード結果は `vendorWriteAcquire()` で借りた DMA バッファへ直接書き、`vendorWriteSubmit()` で投げる
- `Panel_Dl1xx.hpp` は `lgfx::Panel_Device` を派生し、`setWindow` / `drawPixelPreclipped` / `writeFillRectPreclipped` / `writeBlock` / `writePixels` / `writeImage` を実装する。`isReadable()` は false、read 系は未実装。LovyanGFX 内部の RGB565 はリトルエンディアンなので、RLE エンコード中にバイトスワップする（追加コストなし）

`.ino` の想定:

```cpp
#include <LovyanGFX.hpp>
#include <LGFXVirtualCanvas.h>
#include <EspUsbHost.h>
#include "Panel_Dl1xx.hpp"

EspUsbHost usb;
LGFX_Dl1xx lcd(usb);
LGFXVirtualScreen screen(lcd);

void drawScene(LGFXVirtualCanvas &g)
{
    g.fillScreen(TFT_BLACK);
    g.setTextSize(4);
    g.drawString("Hello Full HD", 100, 100);
}

void setup()
{
    Serial.begin(115200);
    usb.begin();
    lcd.onReady([] { screen.invalidate(); });
    screen.setDiffMode(LGFXVirtualDiffMode::Tile);
}

void loop()
{
    if (lcd.ready())
    {
        screen.render(drawScene);
    }
}
```

`EspUsbHost` は内部で USB client task を回すため、`loop()` 側でのポーリングは不要。

`sketch.yaml` の libraries:

```yaml
libraries:
  - LovyanGFX (1.2.21)
  - LGFXVirtualCanvas (1.2.0)
  - dir: ../../../
```

LGFXVirtualCanvas 1.2.0 が Library Manager のインデックスに反映されるまでは、ローカル開発中のみ `- dir: ../../../../LGFXVirtualCanvas` に差し替える。コミット前にバージョン指定へ戻す。バージョンゲートが必要になった場合は `LGFXVIRTUALCANVAS_VERSION_MAJOR` / `_MINOR`（1.0.2 以降で定義）が使える。

## ADB example の扱い

`EspUsbHostAdbConnect` の構造は変えない。ADB はユーザー側でコマンド組み立てとステート管理が必要で、ヘッダに切り出しても `.ino` から中身を触ることになり分離の利得が薄い。

Phase 2 で auto-ZLP と `vendorOutPacketSize()` が入った時点で、手書き ZLP と `getEndpoints()` 走査（約 20 行）を削除する縮小だけ行う。

## テスト方針

### unit

`tests/unit/dl1xx/` を追加する。`Dl1xxProtocol.hpp` をホスト上の g++ でコンパイルして検証する。

- LFSR16: N = 0, 1, 2, 3, ... の既知値と一致すること
- RLE エンコーダ: テスト専用のデコーダを書いて round-trip でピクセル列が一致すること
- RLE エンコーダ: 出力が 256 ピクセルあたり 519 バイトを超えないこと（バッファ上限の保証）
- RLE エンコーダ: 単色 256 ピクセルが 10 バイトになること
- コマンド生成: `count` の 256 → `0` エンコード、24bit アドレスのバイト順
- モードテーブル: 各エントリのレジスタ列が期待バイト列と一致すること

### manual

スループット計測を 2 本に分ける。汎用 API の性能とディスプレイ統合の性能を混ぜない。

#### `tests/manual/vendor_bulk_throughput/`

ライブラリ本体の非同期 bulk OUT 単体の実効スループットを測る。DL-1xx を使わず、`EspUsbDevice` の vendor peer デバイス、または任意の bulk OUT を持つ vendor デバイスに対してダミーデータを流す。

振るパラメータ:

- キュー深度: 1 / 2 / 4 / 8
- 1 転送サイズ: 512 B / 2 KB / 8 KB / 16 KB
- 同期 `vendorWrite()` との比較（深度 1 相当のベースライン）
- ESP32-S3 (FS) / ESP32-P4 (FS) / ESP32-P4 (HS)

出力（1 条件 1 行、既存テストの `MSC_CACHE_RESULT` と同じプレフィックス方式）:

```
VENDOR_BULK_THROUGHPUT mode=async depth=4 xfer=8192 bytes=8388608 elapsed_us=... mbps=... submit_fail=0 errors=0 queue_full=...
```

この結果が Phase 2 の受け入れ条件になる。ここで得られる「Full-speed 実効上限」が、以降のディスプレイ側の数字を正規化する基準になる。

#### `tests/manual/usb_display_throughput/`

DL-1xx + Panel + LGFXVirtualCanvas の統合スループットを測る。描画内容によって RLE 圧縮率が大きく変わるため、パターンを振ることが本質。

描画パターン:

| パターン | 内容 | 狙い |
|---|---|---|
| `solid` | 全画面単色 | RLE 最良（256px → 10B） |
| `gradient_v` | 垂直グラデーション | 行内単色 = 連続領域で最良に近い |
| `gradient_h` | 水平グラデーション | 隣接同色ランがやや長い |
| `text_ui` | 単色背景 + テキスト + 矩形 | 実 UI 相当 |
| `checker1` | 1px 市松 | RLE 最悪（生より膨張） |
| `noise` | 疑似乱数ピクセル（固定 seed） | 最悪ケース その 2 |

更新パターン:

| パターン | 内容 |
|---|---|
| `full` | 毎フレーム全画面が変化 |
| `clock` | 小矩形 1 箇所のみ変化（1〜2 タイル相当） |
| `static` | 2 フレーム目以降まったく変化しない |

その他の軸:

- 差分転送 ON / OFF（`setDiffMode(Tile)` / `Off`）
- 解像度: 800x600 / 1280x720 / 1920x1080
- タイル予算: 既定（19 KB）/ 64 KB（`setMemoryLimit()`）

計測指標:

- エンコード時間（CPU、RLE のみ）
- 送出バイト数と圧縮率（送出バイト / 生ピクセルバイト）
- USB 転送時間と実効 MB/s
- フレーム時間（`render()` 開始から `vendorWriteFlush()` 完了まで）と fps
- `diffPushedPixels()` / `diffTotalPixels()`
- ボトルネック判定: `vendorWritePending()` を定期サンプリングし、キューが空だった割合を出す。空が多ければ CPU（エンコード）律速、常に埋まっていれば USB 律速

出力:

```
DISPLAY_THROUGHPUT pattern=text_ui update=clock diff=on res=1920x1080 tile_kb=19 frames=10 \
  raw_bytes=... tx_bytes=... ratio=... encode_us=... tx_us=... frame_us=... mbps=... fps=... \
  pushed_px=... total_px=... queue_empty_pct=... errors=0
```

シリアル出力自体が計測を汚すため、`esp_timer` で計測して条件終了後にまとめて出力する。

PASS/FAIL は絶対性能では判定しない（ハードウェア依存のため）。判定条件は次のみ:

- 全条件が完走すること
- 転送エラーが 0 であること
- エンコードバッファのオーバーフローが 0 であること
- `diff=on` と `diff=off` で表示結果が目視で一致すること（オペレーター確認）
- `static` + `diff=on` で 2 フレーム目以降の送出バイトが 0 であること

python 側は出力をパースして表形式で表示し、結果を README に転記できる形にする。

#### `tests/manual/usb_display_dl1xx/`

機能確認。スループットとは分ける。

- vendor descriptor から最大画素数を読み、対応モードを列挙する
- EDID を読んでモニタの対応解像度を表示する
- モード設定して単色・カラーバー・テキストを表示する（目視）
- モニタを抜き差しして黒画面になったあと、モードレジスタ再送で復帰することを確認する
- USB を抜き差しして再接続後に再初期化と全画面再描画ができることを確認する
- DL-165 と DL-120/160 世代の両方で実行する

### peer

`tests/peer/usb_vendor` に非同期 bulk OUT の自動テストを追加する。`EspUsbDevice` の vendor peer が受信バイト数を報告できるため、`vendorWriteAsync()` / acquire+submit の正常系・キュー満杯・flush・統計値の整合を自動化できる。スループットの絶対値は peer 側の処理速度に左右されるため、自動テストでは「データが欠落・重複せず届く」ことと統計の整合のみを検証し、性能測定は manual に置く。

## 実装段階

### Phase 0: 下地

- `LICENSE`（MIT）と README のライセンス節 — 完了
- 命名・商標方針の確定 — 本文書

### Phase 1: `vendorOpen()` の緩和 — 完了

- bulk OUT のみのインタフェースを許可
- bulk IN があるときのみ IN 転送を張る
- 同一 interface に複数の bulk endpoint があるとき descriptor 順で最初を選ぶ
- `endpointChannelCount` の加算を実数に修正（OUT のみなら +1）
- `vendorOutPacketSize()` / `vendorInPacketSize()` / `vendorOutEndpoint()` / `vendorInEndpoint()` の追加
- `vendor-api-spec.ja.md` の該当記述を更新
- `tests/manual/vendor_bulk_out_only` を追加し、DL-165 実機で PASS（`out_ep=0x01`、`in_mps=0`、channels 0→1、reopen 冪等）
- `tests/peer/usb_vendor`（bulk IN/OUT ペア経路）3件と `examples/` の esp32s3 ビルドが回帰しないことを確認

### Phase 2: 非同期 bulk OUT キュー — 完了

- 転送プール、acquire / submit / release、`vendorWriteAsync()`
- 統計（`vendorWriteStats()`）、`vendorWriteFlush()`、`vendorWritePending()` / `vendorWriteQueueFree()`
- auto-ZLP（`vendorSetAutoZlp()`）と `vendorWriteZlp()`
- `tests/manual/vendor_bulk_throughput` で実効スループットを確定 → **1.098 MB/s（FS 上限の約 90%）、depth 2 で全転送サイズが上限に張り付く**
- `EspUsbHostAdbConnect` と `tests/manual/adb_connect` の ZLP 処理を auto ZLP に移行（Android 実機での確認は未実施）
- `tests/peer/usb_vendor` への非同期経路の追加は未実施（既存 3 件の回帰は確認済み）

実測から得られた設計上の指針:

- **depth 2 で十分**。depth 4 / 8 は Full-speed では改善しない。メモリはバッファサイズに回すべき
- **転送サイズを小さくしても帯域が落ちない**のがキューの本質的な利点。同期版は 512 byte で 0.88 MB/s（上限の 80%）まで落ちるが、キューは 512 byte でも 1.098 MB/s を維持する。DL-1xx の RLE コマンド列はチャンクが小さくなりがちなので、この性質が直接効く
- 計測時の `queue_empty_pct` は 0〜6%、`queue_full` は多数。producer（CPU）ではなくバスが律速という理想的な状態にある

### Phase 3: DL-1xx プロトコル層

- `Dl1xxProtocol.hpp`（LFSR16、RLE、コマンド生成）
- `Dl1xxModes.hpp`（1920x1080 / 1280x720 / 1024x768 / 800x600 @60）
- `tests/unit/dl1xx`

### Phase 4: デバイス層

- `Dl1xxDevice.hpp`: 検出、claim、チャネルキー、vendor descriptor、EDID、モード設定、ピクセル送出
- `tests/manual/usb_display_dl1xx` で実機に画を出す
- ここで「ESP32-S3 から Full HD が出る」ことを確認する

### Phase 5: LovyanGFX panel

- `Panel_Dl1xx.hpp` / `LGFX_Dl1xx`
- ready コールバックと世代カウンタ
- HPD 復帰時のモード再送

### Phase 6: LGFXVirtualCanvas 統合とスループット計測

- `EspUsbHostDisplayDl1xx.ino`
- `tests/manual/usb_display_throughput`
- 実測値を example README に記録

### Phase 7: ドキュメント反映

- README 対応表に「USB graphics adapter (DL-1xx bulk protocol)」を追加
- API リファレンスに非同期 bulk OUT の節を追加
- example README に参照元と商標表記
- `CHANGELOG.md`
- `docs/FOOTPRINT.md` の probe 追加を検討

## 受け入れ条件

本体 API:

- `vendorOpen()` が bulk OUT のみの vendor インタフェースで成功する
- 非同期 bulk OUT が USB client task 上のコールバックから呼べる
- `vendor_bulk_throughput` で、同期 `vendorWrite()` に対して非同期・深度 2 以上が明確に高いスループットを出す
- `vendorWriteStats()` の submitted / completed / bytes が実送出と整合する
- auto-ZLP を off にした場合の挙動が従来と同一である
- 既存の Vendor / ADB / Network / Audio / MSC の manual・peer テストが回帰しない

example:

- ESP32-S3 + DL-165 で 1920x1080 が表示される
- LGFXVirtualCanvas 経由で描いた内容が正しく表示される
- 差分転送 ON / OFF で表示結果が一致する
- `static` + 差分転送 ON で定常状態の送出バイトが 0 になる
- モニタ HPD による黒画面からモードレジスタ再送で復帰する
- USB 抜き差し後に再初期化して全画面が復帰する
- `usb_display_throughput` の全条件が転送エラー 0 で完走し、結果表が README に記録されている

## 未確認事項・リスク

1. **手元の DL-165 の vendor descriptor が申告する最大画素数**。SKU によってはチップ性能より低く（1600x1200 等に）制限されている。Full HD モニタ接続時に解像度的に問題ないことは確認済みだが、descriptor の申告値そのものは未読なので Phase 4 の最初に確認する
2. **ESP32-P4 の OUT バッファのキャッシュ書き戻し**。2.5.2 で IN 側の `esp_cache_msync()` 対応を入れたが、OUT 側（CPU 書き込み → DMA 読み出し）の write back が ESP-IDF 側で保証されているか未確認。高レートの OUT で P4 だけデータが化けるなら submit 直前に `esp_cache_msync(..., C2M)` が必要。ESP32-S3 は DMA メモリがキャッシュされないため影響なし
3. **電源**。DL-1xx アダプタの消費電流は大きい。OTG コネクタから給電できないボードではセルフパワードハブか外部給電が必須
4. **interrupt IN を開かないことによる副作用**。udl / udlfb も未使用なので問題ないと見ているが実機確認事項
5. **モニタ側が 1920x1080 を EDID で申告しない場合**。テーブルからの強制設定も用意する
6. **DL-120/160 世代での挙動差**。同一プロトコルとされているが、レジスタの一部やパディング要件に差がある可能性がある
7. ~~**Full-speed の実効スループット**~~ → 1.098 MB/s と実測確定（Phase 2）
8. **2 本目の bulk OUT（`0x0a`）の用途**。本実装では使わないが、`0x01` だけで足りることを Phase 4 で確認する
9. **HCD チャネル**。DL アダプタ単体なら 1 チャネルで足りる（実測 0→1）。ただしハブ経由で他デバイスを足すと ESP32-S3 の 8 チャネルはすぐ枯渇する（hub + DL + hub + touchscreen の構成で `No more HCD channels available` を実測）。ディスプレイ検証時はアダプタを直結する

## 未決事項

- 非同期 bulk OUT のプールをデバイス単位にするか endpoint 単位にするか（複数 vendor interface の同時 open を将来入れる場合に影響）
- `vendorWriteAsync()` で `length > bufferBytes` を失敗にするか自動分割するか
- `onVendorWriteComplete()` コールバックを入れるか（統計とキュー空き数で足りるかを実測で判断）
- `ESP_USB_HOST_VENDOR_WRITE_QUEUE_MAX_DEPTH` の既定値（初期案 8）
- 24bpp デュアルプレーンをどの段で入れるか
- `AF 6A` 画面内矩形コピーを `Panel_Dl1xx::copyRect()` に接続するか
- example ディレクトリを `Vendor/` に置くか `Display/` を新設するか
- スループット結果を example README に置くか `docs/` 側に置くか
