# EspUsbHost ALIENTEK DP100 (HID 数控電源) 対応 仕様案

> **English readers:** analysis notes for the ALIENTEK DP100 (`2e3c:af01`), a bench power
> supply that carries its own framed protocol inside 64-byte HID reports, including the
> frame layout, field offsets, scales and the CRC-16/MODBUS. The English summary is in
> [`examples/HID/EspUsbHostDp100Power/README.md`](../examples/HID/EspUsbHostDp100Power/README.md).

## 目的

ALIENTEK (正点原子) の数控直流電源 DP100 / ATK-MDP100 (`2e3c:af01`) を EspUsbHost から制御できるようにする。

DP100 のプロトコル処理はライブラリ本体に入れない。**本体への追加は想定していない**（既存の HID API で足りる、後述）。プロトコル層と機種層は `examples/` 側に置く。この分割方針は `usbtmc-spec.ja.md` / `usb-display-spec.ja.md` / `vendor-api-spec.ja.md` と同じ立場である。

## カテゴリは `examples/HID/`

`examples/` のカテゴリは「デバイスの USB class」ではなく **「ライブラリ本体のどの API を使う example か」** で分かれている。DP100 は HID interface を HID API で駆動するので `examples/HID/` に置く。

同じ「電源を制御する example」でも、USBTMC の PMX18-5A は vendor bulk/control API を使うので `examples/Vendor/EspUsbHostUsbtmcScpi/` にある。**用途ではなく API で決まる**という一貫性を保つ。

## 対象デバイスの実測 descriptor

`tests/manual/device_dump` の出力（ESP32-S3 に直結）。

```
Address 1 portId=0x01 parent=root root_port=1 speed=full-speed
VID:PID 2e3c:af01 class=0x00(per-interface) subclass=0x00 protocol=0x00
Supported=yes hub=no
USB 2.00 device 2.00 ep0=64
Strings manufacturer="ALIENTEK" product="ATK-MDP100" serial="16A1C1C74000"
Configuration value=1 interfaces=1 total_len=41 attributes=0x80(bus-powered) max_power=100mA
Endpoint channels claimed=2/8 managed=1 descriptor_endpoints=2
  Interface 0 alt=0 class=0x03(HID) subclass=0x00 protocol=0x00 endpoints=2 claimed=yes claim=ESP_OK
    Endpoint iface=0 ep=0x81 dir=IN  type=interrupt max_packet=64 interval=1 attrs=0x03
    Endpoint iface=0 ep=0x01 dir=OUT type=interrupt max_packet=64 interval=1 attrs=0x03
```

読み取れること。

- HID interface 1 本のみ。subclass 0x00 / protocol 0x00 なので boot キーボード / マウスではない
- interrupt IN `0x81` と interrupt **OUT** `0x01`、どちらも 64 バイト・interval 1ms。64 バイト固定長のレポートを往復させる形式
- 既に `claimed=yes` / `claim=ESP_OK`。IN endpoint はライブラリがポーリング済み（`managed=1`）
- `max_power=100mA` を申告するが本体は外部給電なので host 側の負担は無い

## 一次情報とライセンス

**ALIENTEK 公式のプロトコル文書は公開されていない。** 配布されているのは Windows 用 DLL (`ATK-DP100DLL(x64)_2.0.dll`) だけで、公開されている情報はすべてその逆解析かバススニッフィング由来である。したがって一次情報の軸は**実機実測**に置き、公開文書は事実の裏取りに限定する。

| 参照元 | ライセンス | 扱い |
|---|---|---|
| 実機実測（`tests/probe/dp100`） | — | **一次情報**。確定したバイト列を example README に残す |
| [ElluIFX/DP100-PyQt5-GUI](https://github.com/ElluIFX/DP100-PyQt5-GUI) | Unlicense (public domain 相当) | 参照する |
| [scottbez1/webdp100](https://github.com/scottbez1/webdp100) | Apache-2.0 | 参照する |
| [lessu/open_dp100](https://github.com/lessu/open_dp100) `DP100_Protocol.md` | **LICENSE ファイル無し** | 事実の裏取りにのみ使う。表や文面は転記しない |
| [weigu1/dp100_manipulator](https://github.com/weigu1/dp100_manipulator) | GPL-3.0 | **参照しない** |
| ALIENTEK の Windows DLL | 商用 | **逆解析しない** |

フレーム構造・OpCode・CRC といった相互運用のための事実は上記の独立した複数プロジェクトで一致しており、どれか一つの文書に依存しない形で実装できる。本ライブラリは MIT。GPL 実装のコードは取り込まない。

## 命名と商標

「ALIENTEK」「正点原子」「DP100」「ATK-MDP100」は広州市星翼電子科技有限公司の商標。指名的使用（対応ハードウェアの特定）に限る。

- example README に商標表記を置く: `ALIENTEK and DP100 are trademarks of their respective owner. This project is not affiliated with, endorsed by, or certified by ALIENTEK.`
- 汎用層のファイル名・識別子は `Dp100` を使う
- 「対応」「認定」など認証を示唆する表現は使わない

## 非目的

- ファームウェア更新（`START_TRANS` / `DATA_TRANS` / `END_TRANS` / `DEV_UPGRADE`）。書き換えを誤ると機器が起動しなくなる
- 電圧・電流スキャン出力（`SCAN_OUT`）と連続出力（`SERIAL_OUT`）のシーケンス機能
- グループ（プリセット）設定の全項目管理。読み書きの経路は通すが UI は作らない
- 本体への DP100 専用 API の追加
- DP100 以外の ALIENTEK 機器

## 本体への追加は無し（既存 HID API で足りる）

実測 descriptor に対して、必要な両方向が既存 API で揃っている。

| 方向 | API | 根拠 |
|---|---|---|
| Host → DP100 | `sendHIDVendorOutput(data, length, address)` | `EspUsbHost.cpp` の endpoint 登録が「HID interface の interrupt OUT」を無条件に `hasVendorOutEndpoint` として登録するため、DP100 の `0x01` がそのまま対象になる。送信はレポート ID を前置せず生バイトを interrupt OUT に流す |
| DP100 → Host | `onHIDInput(callback)` | HID の IN レポートを**レポート ID による振り分けの前に**全件渡す経路。DP100 の 1 バイト目は独自フレームのヘッダなのでレポート ID とは一致せず、`onHIDVendorInput()` には落ちてこない。`onHIDInput()` なら確実に届く |

`onHIDVendorInput()` ではなく `onHIDInput()` を使うのが要点。前者は 1 バイト目が `ESP_USB_HOST_HID_REPORT_ID_VENDOR` の場合しか呼ばれない。

制約が 2 つある。

- `onHIDInput()` は USB task 上で呼ばれる。コールバックでは受信フレームを保存するだけにして、送信と応答待ちは `loop()` 側で行う
- `sendHIDVendorOutput()` は完了を待たない非同期送信。request/response の対応付け（OpCode 一致の確認、タイムアウト、再送）は example 側の責務

この 2 点は example の device 層に閉じ込める。

## プロトコル（公開情報から読める骨格）

以下は着手前に公開情報から読めた骨格である。**すべて `tests/probe/dp100` で実機確認したうえで実装した**（結果は「実測で確定したこと」）。

### フレーム

| offset | 内容 |
|---|---|
| 0 | 方向: Host→Device `0xFB`、Device→Host `0xFA` |
| 1 | OpCode |
| 2 | reserved |
| 3 | `Len`（データ長） |
| 4..3+Len | データ |
| 4+Len | CRC16 下位 |
| 5+Len | CRC16 上位 |

USB レポートは 64 バイト固定で、残りはゼロ埋め。値はリトルエンディアン。CRC は **CRC-16/MODBUS**（多項式 0xA001 反射、初期値 0xFFFF）で、先頭からデータ末尾までを対象にリトルエンディアンで付ける。

### OpCode

| 値 | 名前 | 種別 |
|---|---|---|
| 0x10 | DEVICE_INFO | 読み取り |
| 0x12 | START_TRANS | 非目的（更新） |
| 0x13 | DATA_TRANS | 非目的（更新） |
| 0x14 | END_TRANS | 非目的（更新） |
| 0x15 | DEV_UPGRADE | 非目的（更新） |
| 0x30 | BASIC_INFO | 読み取り |
| 0x35 | BASIC_SET | 読み書き（出力制御を含む） |
| 0x40 | SYSTEM_INFO | 読み取り |
| 0x45 | SYSTEM_SET | 書き込み |
| 0x50 | SCAN_OUT | 非目的 |
| 0x55 | SERIAL_OUT | 非目的 |
| 0x80 | DISCONNECT | 制御 |

### payload

- `DEVICE_INFO (0x10)`: 機種名 `uint8[16]`、hdw_ver / app_ver / boot_ver / run_area（各 `uint16`）、シリアル `uint8[12]`、年 `uint16`、月・日（各 `uint8`）
- `BASIC_INFO (0x30)`: vin, vout, iout, vo_max, temp1, temp2（各 `int16`）、dc_5v（`uint16`）、out_mode, work_st（各 `uint8`）
- `BASIC_SET (0x35)`: index, state（各 `uint8`）、vo_set, io_set, ovp_set, ocp_set（各 `uint16`/`int16`）。index で読み出し、state のフラグで有効化を指示する形

着手時点では**単位が未確定**だった（電圧・電流は mV / mA と推測、`vo_max` と温度のスケールは不明）。実測で確定した内容は次節にある。

## probe 方針（`tests/probe/dp100`）— 完了

`rcs300_felica` と同じ「バイトポンプ + host 側から探索」形式。sketch は Serial から 1 行ずつコマンドを読む。

```
?               HID interface の情報
o <op> [hex]    OpCode + データからフレームを組んで送信（方向・Len・CRC は自動）
x <hex>         生バイトをそのまま送信（64 バイトへゼロ埋め）
v <n>           CRC の変種（0=MODBUS LE / 1=MODBUS BE / 2=方向バイトを除外 / 3=CRC なし）
d <hex>         Host→Device の方向バイト
t <ms>          応答タイムアウト
```

読み取り専用の第 1 テストと、書き込みを行う第 2 テスト（`test_dp100_setpoint_probe`）に分けた。後者は電源の出力を実際に投入するため、出力端子に何も接続していないことを前提とし、値は 5.000V / 0.500A に抑え、最後に必ず出力 OFF と元の設定値に戻す。`SYSTEM_SET (0x45)` と更新系 OpCode は一切撃たない。

## 実測で確定したこと

### CRC とフレーム

`DEVICE_INFO` を CRC 変種 0..3 で撃った結果:

| 変種 | 応答 |
|---|---|
| 0 = MODBUS LE（byte 0〜データ末尾） | **40 バイトの本物の payload** |
| 1 = MODBUS BE | `fa 10 00 01 00` |
| 2 = 方向バイトを除外 | `fa 10 00 01 00` |
| 3 = CRC なし | `fa 10 00 01 00` |

- **CRC は CRC-16/MODBUS、byte 0 からデータ末尾まで、リトルエンディアン**で確定
- 副産物として、**受理されなかったフレームも無視されず、OpCode エコー + 1 バイトの `0x00` で応答される**ことが判明した。無応答と区別できるので、フレームの組み立て間違いを検出できる。後に `BASIC_SET` の受理応答が `0x01` であることが分かり、これは「拒否応答」ではなく**ステータスコード**だと確定した（`isSuccess()` / `isFailure()`）
- 読み取り要求の `Len` は 0 でよい（1 バイト付けても応答は変わらない）

### DEVICE_INFO (0x10) — 40 バイト

```
fa 10 00 28 | 41544b2d445031303000ffffffffffff | 0e00 | 0e00 | 0b00 | aa00
            | c7819d000040041622a75005 | e807 | 0c | 02 | 2c52
```

公開情報の構造とフィールド単位で一致。ただし 2 点の注意がある。

- 機種名は `"ATK-DP100"`。USB の product 文字列 `"ATK-MDP100"` とは**別物**
- シリアル 12 バイト `c7819d000040041622a75005` は USB の serial 文字列 `"16A1C1C74000"` とも**別物**。一方が他方から導出されていると思われるが対応は不明

hdw_ver=14, app_ver=14, boot_ver=11, run_area=0x00aa, 2024年12月2日。

### BASIC_INFO (0x30) — 16 バイト

```
802f 0000 0000 182e 2a01 2401 cb13 0200
```

出力 OFF・無負荷で読み、繰り返して動く値を観察して単位を決めた。

| offset | フィールド | 実測値 | 単位 |
|---|---|---|---|
| 0 | vin | 12160 | mV（約 12.16V の入力に一致） |
| 2 | vout | 0 | mV（出力 OFF） |
| 4 | iout | 0 | mA（出力 OFF のためスケールは未確認） |
| 6 | vo_max | 11800 | mV（現在の入力で出せる上限） |
| 8 | temp1 | 298 | 0.1℃ = 29.8 |
| 10 | temp2 | 292 | 0.1℃ = 29.2 |
| 12 | dc_5v | 5067 | mV（内部 5V レール） |
| 14 | out_mode / work_st | 2 / 0 | 各 1 バイト |

vin・温度・dc_5v は読むたびに数 mV / 0.1℃ 単位で動き、他は静止する。これが値の同定根拠。

`vo_max` は機種の 30V 定格ではなく**現在の入力で出せる上限**なので、設定値の妥当性はこの値に対して確認すべきである。

### SYSTEM_INFO (0x40) — 8 バイト

`50 00 1a 04 02 02 01 00`。フィールドの意味は未確定なので、example では生バイトのまま出す。

### BASIC_SET (0x35) — 10 バイト、書き込みと出力 ON/OFF

| offset | フィールド |
|---|---|
| 0 | index（**フラグ付き**） |
| 1 | state — 出力 ON/OFF（1 で ON、0 で OFF） |
| 2 | 電圧設定値 mV |
| 4 | 電流設定値 mA |
| 6 | 過電圧しきい値 mV |
| 8 | 過電流しきい値 mA |

**index のフラグが本質で、間違えても何も言われない。**

- 読み出し: `35 <index|0x80>`（Len=1）
- 書き込み: `35 <index|0x20> ...`（Len=10）

素の index、`index|0x80`、`index|0xa0` への書き込みはいずれも**成功ステータス `0x01` を返した上で完全に無視される**。応答から区別する手段が無いため、`Dp100Device` はステータスを信用せず読み戻し、manual テストは各段を `BASIC_INFO` で確認する。実機で index と state を 3 巡スイープしても何も起きず、`0x20` を試して初めて動いた（このフラグは Apache-2.0 の `scottbez1/webdp100` で判明。同プロジェクトも「ビットマスクらしいが意味は不明」とコメントしている）。

`Len=0` は設定値として応答されない。**代わりに `BASIC_INFO` フレームが返る**ため、応答は必ず OpCode で照合する必要がある。

index はプリセット群で、index 0 が電源が実際に使う設定。実測値: 0 = 4000mV/3000mA、1 = 2000mV/1000mA、2 = 3000mV/1500mA、3 = 4000mV/2000mA、いずれも ovp 30500mV / ocp 5050mA（30V / 5A の定格に一致）。

**state バイト = 出力 ON/OFF は実機でしか確認できない性質のもの**で、書き込みの応答は出力について何も語らない。設定値 5000mV に対して state=0x01 で `BASIC_INFO` の vout が 5000mV（実測 4.998V）、state=0x00 で 0mV になることを確認した。設定値の書き込み単体では出力は入らず、ovp / ocp はフレームに載せた値がそのまま保たれる。

### 1 バイト応答はステータスコード

当初「拒否応答」と解釈していたが、正しくは**ステータス**である。`0x00` が失敗（誤った CRC が返すもの）、`0x01` が成功（受理された `BASIC_SET` が返すもの）。`isSuccess()` / `isFailure()` で判別する。

## example 構成

`examples/HID/EspUsbHostDp100Power/`

| ファイル | 内容 |
|---|---|
| `Dp100Protocol.hpp` | フレーム組み立て / 解析、CRC-16/MODBUS、payload の構造体化。Arduino / USB 依存なし |
| `Dp100Device.hpp` | USB 層。`onHIDInput()` での受信ラッチ、`sendHIDVendorOutput()` での送信、OpCode 一致の確認、タイムアウト、1 回の再送 |
| `Dp100Power.hpp` | 意味づけ層。ボルト・アンペア・摂氏 |
| `EspUsbHostDp100Power.ino` | 接続 → 機器情報 → 実測値の定期表示 |
| `README.md` / `README.ja.md` | 出典と各参照元のライセンス、商標表記、`HID/` に置いた理由、実測ログ |
| `sketch.yaml` | `esp32s3` / `esp32p4` / `esp32s2` などのプロファイル |

device 層が引き受ける制約は 2 つ。`onHIDInput()` は USB task 上で呼ばれるのでコールバックはコピーとフラグ立てのみ。`sendHIDVendorOutput()` は完了を待たないので、要求と応答の対応付け（OpCode 一致、タイムアウト、再送）は device 層で行う。別 OpCode のフレームは捨てる（返すと対応付けが永久に 1 つ遅れる）。

## テスト — 実施済み

### unit（`tests/unit/dp100`）

- CRC-16/MODBUS を標準チェック値 `"123456789"` → `0x4b37` と、テーブル方式の独立実装（レポート全長までの全長さ）で検証
- 要求フレームのバイト単位一致。データがある場合の CRC 位置と、残りがゼロであること
- 実機キャプチャに対する応答解析と、拒否すべき全ケース（短い読み取り、方向バイト不一致、CRC 不一致、本体の破損、読み取り長を超える Len）
- 1 バイト `0x00` の拒否応答の判定
- `DEVICE_INFO` / `BASIC_INFO` のフィールドオフセットと単位を実機キャプチャで固定
- `BASIC_SET` の往復（オフセットは実機未確認である旨をコード側に明記）

### manual（`tests/manual/dp100`）

読み取り専用なので、負荷を接続したままでも安全。

```
device type="ATK-DP100" hw=14 app=14 boot=11 run_area=0x00aa built=2024-12-02 serial=c7819d000040041622a75005
basic in=12.160V out=0.000V 0.000A max_out=11.800V rail5v=5.067V temp=29.6/29.0C mode=2 status=0
system info raw=50001a0402020100 len=8
repeated reads done refusals=0 received=53
interleaved reads done
[PASS]
```

- 機種名に `DP100` を含むこと、ビルド日付が妥当な範囲にあること（オフセットの検証）
- 入力電圧が USB 入力として妥当、内部 5V レールが 4〜6V、温度が -20〜120℃（スケールの検証）
- 50 回連続読み出しで拒否 0
- `DEVICE_INFO` / `BASIC_INFO` を交互に 5 ラウンド（要求と応答の対応付けの検証）

### peer / loopback

DP100 のプロトコルを実装した peer は無いため対象外。

## 受け入れ条件 — 読み取りは全て達成

- `DEVICE_INFO` が `ATK-DP100` と妥当なビルド日付を返す — 達成
- `BASIC_INFO` の各値が物理的に妥当 — 達成
- 連続・交互取得で CRC 不一致・拒否・取り違えなし — 達成
- `tests/unit/dp100` が通る — 達成
- `python tools/build_check.py esp32s3` が通る — 達成
- 設定値の書き込みと読み戻し — 達成（`tests/manual/dp100_output`）
- 出力 ON/OFF が実際に端子の電圧を変えること — 達成（5.000V 設定で 4.998V 実測）

## 残っている未確認事項・リスク

1. **iout のスケール**。無負荷でしか試していないため 0 しか観測しておらず、mA と断定できていない。負荷をかけた確認が必要
2. **`index|0x20` の 0x20 が何を意味するのか**。書き込みに必要なことは実測で確定したが、ビットの意味は参照元も不明としている
3. **`SYSTEM_INFO` のフィールド意味**。生バイトのまま公開している
4. **`SYSTEM_SET (0x45)`**。設定を永続化する可能性があるため触っていない
5. **抜き差し後の再接続**。`.ino` は disconnect で `end()` して再接続を試みるが、manual テストは 1 回の接続で完結するため未検証
6. **`onHIDInput()` の単一コールバック**。`Dp100Device::begin()` がスケッチ全体の `onHIDInput()` を占有する。他の用途と併用する場合は、スケッチ側で `onHIDInput()` を持ち `acceptReport()` に転送する（そのための API を用意済み）
7. **ハブ経路のクラッシュ**。`ext_hub` の assert は別課題。DP100 は直結で検証した

## 未決事項

- `SYSTEM_INFO` のフィールドを解明するか
- `SCAN_OUT` / `SERIAL_OUT` を扱うか
- グループ（プリセット）を扱うか
