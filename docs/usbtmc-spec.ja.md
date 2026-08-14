# EspUsbHost USBTMC (SCPI 計測器) 対応 仕様案

> **English readers:** analysis notes for USBTMC / USB488 (interface class `0xfe`,
> subclass `0x03`) and SCPI, written while implementing the instrument example against a
> KIKUSUI PMX18-5A (`0b3e:1029`). The English summary — the class requests, the bulk
> message layer and the SCPI wrapper — is in
> [`examples/Vendor/EspUsbHostUsbtmcScpi/README.md`](../examples/Vendor/EspUsbHostUsbtmcScpi/README.md).

## 目的

USBTMC (USB Test and Measurement Class) の計測器を EspUsbHost から使えるようにする。最初の対象は菊水電子工業の直流電源 PMX18-5A (`0b3e:1029`)。

USBTMC のメッセージ層と SCPI コマンドはライブラリ本体に入れない。本体には汎用の control transfer API を 1 本だけ追加し、USBTMC 固有の処理と機種固有の SCPI ラッパーは `examples/` 側に置く。この分割方針は `vendor-api-spec.ja.md` の vendor bulk/control API、および `usb-display-spec.ja.md` の DL-1xx / AX206 と同じ立場である。

## USBTMC は「Vendor カテゴリ」だが vendor-specific class ではない

**USBTMC の interface class は `0xFE` (Application Specific) / subclass `0x03` であり、vendor-specific class `0xFF` ではない。** それでも example を `examples/Vendor/` に置く。

`examples/` のカテゴリは「デバイスの USB class」ではなく **「ライブラリ本体のどの API を使う example か」** で分かれている。

| example | デバイスの実体 | 使う本体 API | カテゴリ |
|---|---|---|---|
| `Serial/EspUsbHostDisplayTuring` | ディスプレイ | CDC serial API | Serial |
| `Vendor/EspUsbHostDisplayAx206` | Bulk-Only Transport (MSC 系プロトコル) | vendor bulk API | Vendor |
| `Vendor/EspUsbHostAdbConnect` | class `0xFF` | vendor bulk API | Vendor |
| `Vendor/EspUsbHostUsbtmcScpi` | **class `0xFE` USBTMC** | vendor bulk/control API | Vendor |

`Ccid` / `UsbNetwork` / `Storage` / `Audio` のように独立カテゴリを持つのは、本体に専用 API があるクラスだけである。USBTMC は本体に専用 API を持たない（`usbtmcOpen()` のようなものは追加しない）ため `Vendor/` に属する。

将来 USBTMC を本体の一級クラスとしてサポートする判断をした場合は、そのタイミングで `examples/Usbtmc/` に昇格させる。README とこの節に分類軸を明記しておき、「Vendor = class 0xFF」という誤読を防ぐ。

## 対象デバイスの実測 descriptor

`tests/manual/device_dump` の出力（ESP32-S3 + 4 ポートハブ経由）。

```
Address 2 portId=0x11 parent=1 speed=full-speed
VID:PID 0b3e:1029 class=0x00(per-interface) subclass=0x00 protocol=0x00
Supported=no hub=no
USB 2.00 device 1.00 ep0=64
Strings manufacturer="KIKUSUI" product="PMX18-5A" serial="DR000046"
Configuration value=1 interfaces=1 total_len=39 attributes=0xc0(self-powered) max_power=32mA
  Interface 0 alt=0 class=0xfe subclass=0x03 protocol=0x01 endpoints=3 claimed=no
    Endpoint iface=0 ep=0x01 dir=OUT type=bulk      max_packet=64  interval=0
    Endpoint iface=0 ep=0x82 dir=IN  type=bulk      max_packet=64  interval=0
    Endpoint iface=0 ep=0x83 dir=IN  type=interrupt max_packet=16  interval=100
```

読み取れること。

- `protocol=0x01` は USB488 サブクラス。IEEE 488.2 / SCPI を載せる構成
- bulk OUT `0x01` + bulk IN `0x82` の MPS は 64（full-speed 上限）
- interrupt IN `0x83`（interval 100ms）は USB488 の SRQ (Service Request) 通知用。**初期実装では使わない**
- `max_power=32mA` なので本体側の給電要件は軽い

## 一次情報とライセンス

| 参照元 | 扱い |
|---|---|
| USB-IF `Universal Serial Bus Test and Measurement Class Specification (USBTMC) Revision 1.0` | 参照する（一次情報） |
| USB-IF `USBTMC USB488 Subclass Specification Revision 1.0` | 参照する（一次情報） |
| IEEE 488.2 共通コマンド（`*IDN?` 等）、SCPI 1999 標準コマンド | 参照する（公開標準） |
| 菊水電子工業 PMX シリーズ 通信インターフェースマニュアル | 参照する（機種固有コマンドの典拠） |
| linux-gpib / libusbtmc など GPL 実装 | **参照しない** |

本ライブラリは MIT。GPL 実装のコードは取り込まず、公開仕様からスクラッチ実装する。

## 命名と商標

「KIKUSUI」「PMX」は菊水電子工業株式会社の商標。指名的使用（対応ハードウェアの特定）に限る。

- example README に商標表記を置く: `KIKUSUI and PMX are trademarks of KIKUSUI ELECTRONICS CORPORATION. This project is not affiliated with, endorsed by, or certified by KIKUSUI ELECTRONICS CORPORATION.`
- 汎用層のファイル名・識別子は `Usbtmc` を使い、機種固有部分だけ `ScpiPmx` とする
- 「対応」「認定」など認証を示唆する表現は使わない

## 非目的

- USBTMC を本体の一級クラスとしてサポートすること（自動 claim、専用 API）
- interrupt IN による SRQ / Service Request 通知
- USB488 の `TRIGGER` bulk メッセージ、`REN_CONTROL` / `GO_TO_LOCAL` / `LOCAL_LOCKOUT`（API としては出せるが example では使わない）
- `VENDOR_SPECIFIC_OUT` / `REQUEST_VENDOR_SPECIFIC_IN` メッセージ
- 複数 USBTMC 機器の同時使用（本体の vendor interface は device あたり 1 本の制約に従う）
- SCPI パーサ / 単位変換フレームワーク。応答は文字列と `float` 変換までとする
- 機種横断のコマンド互換レイヤ

## 本体に追加する API

### 追加は 1 本だけ

bulk 側は既存 API で足りる。実測 descriptor に対して:

- `vendorOpen(address, 0, ESP_USB_HOST_VENDOR_READ_ON_DEMAND)` — interface 番号を明示すれば class `0xFE` でも claim される（既存の仕様・コメント済み）。3 つ目の interrupt IN endpoint は「bulk OUT + 使わない interrupt IN」の既存経路で無視される
- `vendorWrite()` / `vendorReadSync()` — request/response 型の USBTMC にそのまま合う
- `vendorOutPacketSize()` / `vendorInPacketSize()` — 4 バイト境界と short packet 判定に使う

足りないのは control transfer の `bmRequestType` である。既存の `vendorControlIn()` / `vendorControlOut()` は `0xC0` / `0x40`（vendor 型・device recipient）固定で、USBTMC が要求する `0xA1` / `0x21`（class 型・interface recipient）と standard 型の `CLEAR_FEATURE(ENDPOINT_HALT)` が出せない。

`vendor-api-spec.ja.md` が「interface recipient を使いたい場合は、後続で request type を指定できる低レベル API を追加検討する」と予告していた箇所にあたる。

```cpp
// Sends one EP0 control transfer with a caller-supplied bmRequestType. The
// typed helpers above cover vendor requests to the device; this is the escape
// hatch for class or standard requests, and for interface or endpoint
// recipients. Waits for completion, so it cannot be called from a USB callback.
bool vendorControlTransfer(uint8_t requestType,
                           uint8_t request,
                           uint16_t value,
                           uint16_t index,
                           uint8_t *data = nullptr,
                           size_t length = 0,
                           size_t *actualLength = nullptr,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                           uint32_t timeoutMs = ESP_USB_HOST_VENDOR_CONTROL_DEFAULT_TIMEOUT_MS);
```

挙動。

- 転送方向は `requestType & 0x80` で決まる。IN のとき `actualLength` に実受信バイト数を返す
- `length > 0 && data == nullptr` は `false`
- 既存の `vendorControlIn()` / `vendorControlOut()` はこの関数に委譲する。外部から見た挙動は変えない（後方互換）
- 対象 device は vendor open 済み device を優先し、無ければ `findDevice(address)`。`vendorOpen()` 前でも EP0 は使える（既存 2 関数と同じ）

これ以上の USBTMC 固有 API（`usbtmcClear()` 等）は本体に入れない。

### 付随する小改善

`printDeviceInfo()` の class 名テーブルが `0xFE` を `Unknown` と表示する。`Application Specific` を追加する。USBTMC / DFU / IrDA がここに入るため、ダンプから素性を読み取れるようにする意味がある。

## USBTMC プロトコル（example 側に実装する範囲）

### bulk OUT ヘッダ（12 バイト）

| offset | 内容 |
|---|---|
| 0 | `MsgID`: `1` = DEV_DEP_MSG_OUT, `2` = REQUEST_DEV_DEP_MSG_IN |
| 1 | `bTag`: 1..255。0 は不可。直前のメッセージと異なる値にする |
| 2 | `bTagInverse`: `~bTag & 0xFF` |
| 3 | reserved (`0x00`) |
| 4-7 | `TransferSize` (LE32)。OUT ではペイロード長、IN 要求では受け入れ最大バイト数 |
| 8 | `bmTransferAttributes`。OUT: bit0 = EOM。IN 要求: bit1 = TermCharEnabled |
| 9 | IN 要求のとき `TermChar`、それ以外 reserved |
| 10-11 | reserved |

ヘッダの後にペイロードを置き、**全体長が 4 の倍数になるまで `0x00` でパディング**する。

### bulk IN 応答

同じ 12 バイトヘッダ（`MsgID = 2`, `bTag` はエコー）+ `TransferSize` バイトのペイロード + 4 バイト境界パディング。`bmTransferAttributes` bit0 が EOM。

デバイスは bulk IN 転送を必ず short packet（長さ 0 を含む）で終端する。よって MPS 64 に対して 1 回の `vendorReadSync()` でヘッダ + ペイロードが取れることが多いが、EOM が立つまでループする実装にする。

### class request（`bmRequestType` = `0xA1` IN / `0x21` OUT, `wIndex` = interface number）

| bRequest | 名前 | 応答 |
|---|---|---|
| 1 | INITIATE_ABORT_BULK_OUT (`wValue` = bTag) | 2 バイト: status, bTag |
| 2 | CHECK_ABORT_BULK_OUT_STATUS | 8 バイト: status, reserved×3, NBYTES_RXD(LE32) |
| 3 | INITIATE_ABORT_BULK_IN (`wValue` = bTag) | 2 バイト |
| 4 | CHECK_ABORT_BULK_IN_STATUS | 8 バイト |
| 5 | INITIATE_CLEAR | 1 バイト: status |
| 6 | CHECK_CLEAR_STATUS | 2 バイト: status, bmClear |
| 7 | GET_CAPABILITIES | 24 バイト |
| 64 | INDICATOR_PULSE | 1 バイト |
| 128 | READ_STATUS_BYTE (USB488, `wValue` = bTag 0x02..0x7F) | 3 バイト: status, bTag, statusByte |

`USBTMC_status`: `0x01` SUCCESS, `0x02` PENDING, `0x80` FAILED, `0x81` TRANSFER_NOT_IN_PROGRESS, `0x82` SPLIT_NOT_IN_PROGRESS, `0x83` SPLIT_IN_PROGRESS。

`GET_CAPABILITIES` の 24 バイトで読む値。

| offset | 内容 |
|---|---|
| 0 | USBTMC_status |
| 2-3 | `bcdUSBTMC` (LE16) |
| 4 | USBTMC interface capabilities: bit2 INDICATOR_PULSE 可, bit1 talk-only, bit0 listen-only |
| 5 | USBTMC device capabilities: bit0 TermChar 対応 |
| 12-13 | `bcdUSB488` (LE16) |
| 14 | USB488 interface capabilities: bit2 USB488.2, bit1 REN_CONTROL 系可, bit0 TRIGGER 可 |
| 15 | USB488 device capabilities: bit3 **SCPI 準拠**, bit2 SR1, bit1 RL1, bit0 DT1 |

offset 15 bit3 で「中身が SCPI」であることをデバイス自身に申告させられる。example の接続シーケンスでこれを表示する。

**USB488 のフィールドは 12/13 ではなく 14/15。** 12-13 は `bcdUSB488` が占める。実装時に 2 バイト手前を読んでいて、全機能対応の PMX18-5A が「何も対応していない」ように見えた。PMX18-5A の実応答:

```
01 00 00 01 00 01 00 00 00 00 00 00 00 01 07 0F 00 00 00 00 00 00 00 00
                                     ^^^^^ ^^ ^^
                                     bcd   IF DEV = SCPI+SR1+RL1+DT1
```

生バイトを出さないと気付けない類の誤りなので、manual テストは decode 結果と生 24 バイトの両方を出す。unit テストはこの実バイト列を回帰として持つ。

### CLEAR シーケンス

1. `INITIATE_CLEAR` を送る。status が SUCCESS でなければ失敗
2. `CHECK_CLEAR_STATUS` を PENDING でなくなるまでポーリング。PENDING かつ `bmClear` bit0 が立っていれば、short packet が来るまで bulk IN を読み捨てる
3. `CLEAR_FEATURE(ENDPOINT_HALT)` を bulk OUT endpoint に送る（standard 型: `bmRequestType = 0x02`, `bRequest = 0x01`, `wValue = 0x0000`, `wIndex` = endpoint address）

**手順 3 は実装しない（実測に基づく決定）。** halt をクリアするとデバイス側のデータトグルが DATA0 にリセットされる。Linux は `usb_clear_halt()` の中でホスト側も合わせるが、ESP-IDF の `usb_host_endpoint_clear()` は実際に halt した pipe しか対象にできないため、正常な endpoint に対してはホスト側トグルを合わせる手段がない。結果としてトグルがずれ、以降の OUT がデバイスに無視される。

実測: 手順 3 を入れると、`begin()` 直後の CLEAR（pipe が fresh で偶然トグルが一致する）は通るが、通信が進んだ後の CLEAR の次のクエリは bulk IN タイムアウトになる。外すと CLEAR は成功し次のクエリも応答する。手順 1-2 だけでデバイス側のバッファはクリアされる。

本当に stall した endpoint は、転送失敗時に pipe を flush / clear する本体の既存経路が回復させる。`clearOutHalt()` は example に実装として残し、呼ばない理由をコメントに書く（standard request を `vendorControlTransfer()` で出す実例にもなる）。

なお `vendorControlTransfer()` の必要性は手順 3 とは独立している。class request 群（`0xa1`）が本来の理由。

## example 構成

`examples/Vendor/EspUsbHostUsbtmcScpi/`

| ファイル | 内容 |
|---|---|
| `UsbtmcProtocol.hpp` | ワイヤフォーマットのみ。12 バイトヘッダの組み立て / 解析、4 バイト境界パディング、bTag 検査、class request の定数と `GET_CAPABILITIES` の構造体化。Arduino / USB 依存なし |
| `UsbtmcDevice.hpp` | USB 層。USBTMC interface の検出、`vendorOpen()`、`GET_CAPABILITIES`、CLEAR、`write()` / `query()`（EOM までのループ、bTag 管理、タイムアウト）、abort によるエラー回復 |
| `ScpiPmx.hpp` | PMX18-5A 用の薄いラッパー。`idn()` / `setVoltage()` / `setCurrent()` / `output()` / `measureVoltage()` / `measureCurrent()` / `error()` |
| `EspUsbHostUsbtmcScpi.ino` | 接続 → capabilities 表示 → `*IDN?` → 設定 → 実測値の定期表示 |
| `README.md` / `README.ja.md` | プロトコル出典、商標表記、Vendor カテゴリの説明、実測ログ |
| `sketch.yaml` | `esp32s3` / `esp32p4` プロファイル |

`ScpiPmx.hpp` を差し替えれば他の USBTMC 機器（オシロ、DMM 等）に流用できる形にする。`UsbtmcDevice.hpp` に機種固有の記述を入れない。

`ScpiPmx.hpp` が使う SCPI は IEEE 488.2 共通コマンドと SCPI 標準の電源系ノードに限る。

| 用途 | コマンド |
|---|---|
| 識別 | `*IDN?` |
| ステータスクリア | `*CLS` |
| 出力電圧設定 / 読み出し | `VOLT <v>` / `VOLT?` |
| 出力電流設定 / 読み出し | `CURR <a>` / `CURR?` |
| 出力 ON/OFF | `OUTP ON` / `OUTP OFF` / `OUTP?` |
| 実測値 | `MEAS:VOLT?` / `MEAS:CURR?` |
| エラーキュー | `SYST:ERR?` |

機種固有の拡張コマンドは使わない。実機で応答しないものがあればマニュアルを典拠に README に記録する。

**安全側の既定値**: example は起動時に出力を OFF にし、電圧・電流の設定値を控えめな固定値（例 5.0V / 0.5A）にしてから ON する。`.ino` の先頭に「実負荷を接続したまま実行しないこと」を明記する。

## テスト方針

### unit

`tests/unit/` の g++ ハーネスで `UsbtmcProtocol.hpp` を単体テストする（Arduino / USB 依存がないため可能）。

- ヘッダ組み立てのバイト列一致（DEV_DEP_MSG_OUT / REQUEST_DEV_DEP_MSG_IN）
- 4 バイト境界パディングの長さ計算
- `bTagInverse` の生成と検証、bTag の 0 回避と巡回
- 応答ヘッダの解析（TransferSize、EOM、bTag 不一致の検出）
- `GET_CAPABILITIES` のビット分解

### manual

`tests/manual/usbtmc_scpi/` を追加する。PMX18-5A 実機が必要。

- USBTMC interface の検出と `vendorOpen()` 成功
- `GET_CAPABILITIES` で SCPI ビットが立つこと
- `*IDN?` に `KIKUSUI` を含む応答が返ること
- 設定 → `MEAS:VOLT?` が設定値の近傍に入ること（出力 OFF のままでも設定値の読み戻しは可）
- `SYST:ERR?` が `0,"No error"` を返すこと
- 抜き差し後の再接続

`tests/manual/device_dump/` は今回追加済み。任意のデバイスの descriptor ダンプ用に汎用化してある。

### peer / loopback

USBTMC を実装した peer device は無いため対象外。`vendorControlTransfer()` そのものは既存の `tests/peer/usb_vendor` 系で `0xC0` / `0x40` 相当の呼び出しに置き換えて回帰確認できる。

## 実装段階

### Phase 0: 調査 — 完了

`tests/manual/device_dump` を追加し、interface class `0xFE` / subclass `0x03` / protocol `0x01`、bulk `0x01` / `0x82`、interrupt IN `0x83` を実測確定。

### Phase 1: 本体 API — 完了

- `vendorControlTransfer()` を追加、既存 2 関数を委譲に変更
- `className()` に `0xFE` = `Application Specific` を追加
- README（en/ja）の API 節と `keywords.txt` を更新

### Phase 2: プロトコル層 — 完了

`UsbtmcProtocol.hpp` と `tests/unit/usbtmc`（`test_usbtmc.py` + `usbtmc_test.cpp`）。

### Phase 3: デバイス層 — 完了

`UsbtmcDevice.hpp`。`GET_CAPABILITIES` → CLEAR → `*IDN?` を実機で確認。ここで CLEAR の halt クリアと capabilities のオフセットの 2 点が実測で修正された。

### Phase 4: SCPI ラッパーと .ino — 完了

`ScpiPmx.hpp`、`EspUsbHostUsbtmcScpi.ino`、`README.md` / `README.ja.md`。

### Phase 5: テストとドキュメント反映 — 完了

`tests/manual/usbtmc_scpi/`、`tests/manual/device_dump/`、README の対応表と example 一覧（en/ja）、`tests/manual/README*`、`tests/unit/README*`、`tests/TEST_PLAN*`、`CHANGELOG.md`。`docs/COMPATIBILITY.*` と `docs/FOOTPRINT.md` はリリース時に生成されるため触らない。

## 受け入れ条件 — 全て達成

`tests/manual/usbtmc_scpi` の実測（ESP32-S3、ハブ経由）:

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
[PASS]
```

- ESP32-S3 で `*IDN?` が `KIKUSUI,PMX18-5A,DR000046,...` を返す — 達成
- `GET_CAPABILITIES` の USB488 device capabilities で SCPI ビットが立つ — 達成（オフセット修正後）
- 電圧・電流設定が反映され、`MEAS:VOLT?` / `MEAS:CURR?` が読める — 達成
- `SYST:ERR?` がエラーなしを返す — 達成（`0,"No error"`）
- 連続クエリ 20 回でタイムアウト / bTag 不一致なし — 達成
- 接続中の CLEAR とその後のクエリが通る — 達成（halt クリアを外した後）
- `tests/unit/usbtmc` が通る — 達成
- `python tools/build_check.py esp32s3` が通る — 達成

## 実測で判明したこと

1. **`GET_CAPABILITIES` の USB488 フィールドは offset 14/15**（12-13 は `bcdUSB488`）。当初 12/13 を読んでいて、全機能対応の機器が「何も対応していない」ように見えた。生バイトのダンプで判明。unit テストに実バイト列を回帰として入れた
2. **CLEAR シーケンスの `CLEAR_FEATURE(ENDPOINT_HALT)` は入れてはいけない**。データトグルがずれ、CLEAR 直後のクエリがタイムアウトする。詳細は「CLEAR シーケンス」節
3. **bulk IN は 1 回の `vendorReadSync()`（要求 512 バイト）でヘッダ + ペイロードが取れる**。short packet 終端が期待通り効いている。EOM までのループと、ヘッダが分割された場合の再読み出しは実装済みだが、この機種では発動しない
4. **クエリ間の待ちは不要**。`REQUEST_DEV_DEP_MSG_IN` の直後に bulk IN を投げても NAK タイムアウトにならない。20 回連続クエリが 1 秒未満で完走する
5. **`INDICATOR_PULSE` は非対応**（capabilities byte 4 = `0x00`）。`indicatorPulse()` は capabilities を見て失敗を返す
6. **USB488 interface capabilities は `0x07`**（TRIGGER / REN_CONTROL 系 / USB488.2 すべて可）。今回は使わないが、後から `TRIGGER` や `REN_CONTROL` を足せる機種であることは確認できた

## 残っている未確認事項・リスク

1. **ABORT_BULK_IN / ABORT_BULK_OUT の実機確認**。実装はあるが、実機で誘発できていない。CLEAR による復帰は確認済み
2. **SRQ（interrupt IN）を使わないこと**。長時間コマンドの完了待ちは `*OPC?` ポーリングで代替する。`READ_STATUS_BYTE` は interrupt IN があるデバイスでは status byte を interrupt IN 側で返す規定なので、この機種では 3 バイト目が有効値にならない可能性がある。使わない方針なので影響しないが README に注記済み
3. **出力 ON を伴う動作**。manual テストは出力を ON にしない。`.ino` は `TURN_OUTPUT_ON = false` を既定とし、無負荷での実行を README と `.ino` 冒頭で求めている
4. **他機種**。オシロ / DMM は未検証。`ScpiPmx.hpp` の差し替えで届く設計にはなっているが、`TermChar` を要求する機種の挙動は未確認
5. **応答が `RESPONSE_CHUNK`（496 バイト）を超えるケース**。EOM が下りた継続読み出しの経路は実装済みだが、PMX の応答はすべて 1 ラウンドに収まるため未実行
6. **抜き差し後の再接続**。`.ino` は disconnect で `end()` して 1 秒間隔で再接続を試みる作りだが、manual テストは 1 回の接続で完結するため未検証。必要なら `hotplug` 相当の手順を足す

## 未決事項

- 他機種（オシロ / DMM）を README の対応表に「未検証」として載せるか
- `TRIGGER` / `REN_CONTROL` / `GO_TO_LOCAL` / `LOCAL_LOCKOUT` を example に足すか（機種は対応を申告している）
- interrupt IN を使う SRQ 対応を本体 API として入れるか（入れる場合は USBTMC 固有ではなく「vendor interface の interrupt IN」として設計する）
