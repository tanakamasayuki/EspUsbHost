# 手動テスト

> English: [README.md](README.md)

自動化できないテストを置くディレクトリです。
各テストが手動である理由は「自動化が面倒だから」ではなく、「環境をソフトウェアで完全に制御できないから」です。

全体のテスト方針と各カテゴリの手動化の根拠は [../TEST_PLAN.ja.md](../TEST_PLAN.ja.md) を参照してください。

## 手動テストの実行方法

手動テストのファイルには `test_` プレフィックスを付けないため、pytestが自動収集しません。
必要なハードウェアを準備してから明示的に実行します：

```sh
cd tests
uv run --env-file .env pytest manual/smoke/smoke.py -v -s
uv run --env-file .env pytest manual/vcp_ftdi/vcp_ftdi.py -v -s
uv run --env-file .env pytest manual/msc_block/msc_block.py -v -s
uv run --env-file .env pytest manual/msc_hotplug_mount/msc_hotplug_mount.py -v -s
uv run --env-file .env pytest manual/msc_cache_coherency/msc_cache_coherency.py -v -s
uv run --env-file .env pytest manual/adb_connect/adb_connect.py -v -s
uv run --env-file .env pytest manual/vendor_bulk_out_only/vendor_bulk_out_only.py -v -s
uv run --env-file .env pytest manual/vendor_bulk_throughput/vendor_bulk_throughput.py -v -s
uv run --env-file .env pytest manual/usb_display_dl1xx/usb_display_dl1xx.py -v -s
uv run --env-file .env pytest manual/usb_display_throughput/usb_display_throughput.py -v -s
uv run --env-file .env pytest manual/usb_display_turing/usb_display_turing.py -v -s
```

手動テストは常に `-s` を付けて実行します。シリアル出力とオペレーターへのプロンプトが端末に表示されます。

デフォルトのボードプロファイルは `esp32s3` です。別のボードを使う場合は `--profile` で指定します：

```sh
uv run --env-file .env pytest manual/smoke/smoke.py -v -s --profile esp32p4
```

使用できるプロファイルは各テストの `sketch.yaml` に定義されています。

## テスト一覧

| テスト | 説明 | 必要なハードウェア | 状態 |
|--------|------|------------------|------|
| [`smoke/`](smoke/) | 手順確認用 — ビルド・フラッシュ・シリアル・オペレータープロンプトが正しく動くことを確認する。機能テストではない。新しいマシンではまずこれを実行する。 | ESP32-S3 または ESP32-P4 | ✅ |
| [`vcp_ftdi/`](vcp_ftdi/) | FTDI VCP（VID 0x0403）経由のTX/RXループバック | FTDIデバイス（FT232Rなど）TXとRXをショート | ✅ |
| [`vcp_cp210x/`](vcp_cp210x/) | CP210x VCP（VID 0x10C4）経由のTX/RXループバック | CP210xデバイス（CP2102など）TXとRXをショート | ✅ |
| [`vcp_ch34x/`](vcp_ch34x/) | CH34x VCP（VID 0x1A86）経由のTX/RXループバック | CH34xデバイス（CH340など）TXとRXをショート | ✅ |
| [`vcp_pl2303/`](vcp_pl2303/) | PL2303 VCP（VID 0x067B PID 0x2303）経由のTX/RXループバック | PL2303デバイス TXとRXをショート | ✅ |
| [`vcp_pl2303gs/`](vcp_pl2303gs/) | PL2303GS VCP（VID 0x067B PID 0x23A3）経由のTX/RXループバック | PL2303GSデバイス TXとRXをショート | ✅ |
| [`esp32_autoreset/`](esp32_autoreset/) | DTR/RTSによるESP32ターゲットのリセットとROMダウンロードモード確認 | ESP32自動リセット回路へ接続したUSBシリアル変換 | ✅ |
| [`keyboard_leds/`](keyboard_leds/) | NumLock・CapsLock LEDの目視確認 | インジケーターLED付きUSBキーボード | ✅ |
| [`multi_hid_keyboard_mouse/`](multi_hid_keyboard_mouse/) | キーボードとマウスを同時接続したとき独立してイベントが届くこと | USBキーボード＋USBマウス | ✅ |
| [`multi_serial/`](multi_serial/) | `setAddress()` で2台のシリアルデバイスが独立して動作すること | TXとRXをショートしたUSBシリアルデバイス×2 | ✅ |
| [`hotplug/`](hotplug/) | 接続・切断イベントの正常発火と繰り返しサイクルでクラッシュしないこと | 任意のUSBデバイス | ✅ |
| [`hub_info/`](hub_info/) | USBハブ経由で接続されたデバイスのトポロジー情報を表示すること | USBハブ＋USBデバイス2台 | ✅ |
| [`hub_power/`](hub_power/) | ポート単位の電源制御 — ハブのポートをOFF/ONしてデバイスの切断・再接続を確認 | ポート単位の電源制御対応USBハブ＋任意のUSBデバイス | ✅ |
| [`usb_network_descriptor/`](usb_network_descriptor/) | configurationを横断して汎用CDC-ECM/CDC-NCM USB Ethernet descriptor候補を検出すること | CDC-ECMまたはCDC-NCM対応USB Ethernetアダプタ | ✅ |
| [`msc_block/`](msc_block/) | 実USBメモリのMSC容量取得、LBA 0読み取り、FatFs/VFS mount、POSIXと`fs::FS` APIで一時ファイルのwrite/read/deleteを確認 | USBメモリ | ✅ |
| [`msc_hotplug_mount/`](msc_hotplug_mount/) | mount中のUSBメモリを抜き、再接続後に同じFatFs/VFS pathへ再mountできることを確認 | USBメモリ | ✅ |
| [`msc_cache_coherency/`](msc_cache_coherency/) | 同じLBA範囲をcache負荷下でmulti-sector readし、single-sector referenceと比較してCPU cacheとUSB DMAの非coherencyを検出（read only） | ESP32-P4 + USBストレージ（ESP32-S3はネガティブコントロール） | ✅ |
| [`adb_connect/`](adb_connect/) | Android実機のADBを許可・永続RSA鍵で認証し、単一shell echo streamを検証すること | USBデバッグを有効にしたAndroid端末＋USBデータケーブル | ✅ |
| [`ccid_info/`](ccid_info/) | interface/endpoint構成を列挙し、CCID interface（class 0x0b）の有無を判定すること | CCIDスマートカードリーダー（Sony RC-S300 PaSoRiなど） | ✅ |
| [`ccid_card/`](ccid_card/) | CCIDリーダーの一連の動作: open、class descriptor、slot状態、power onとATR、Get UID APDUの繰り返し、生のGetSlotStatus | CCIDスマートカードリーダー＋カード | ✅ |
| [`ccid_felica/`](ccid_felica/) | Sony RC-S300のtransparent session経由でSystem Codeを指定したFeliCa IDm取得: session開始、FeliCaへのswitch protocol、RF on/off、ワイルドカード0xffffと交通系0x0003でのPolling、応答からのIDm。比較用にリーダー自前のポーリング結果も出力する | Sony RC-S300＋FeliCaカード（交通系カードなら0x0003も確認できる） | ✅ |
| [`ccid_hotplug/`](ccid_hotplug/) | カードを外して戻したときにslot変化通知がonCcidCardRemoved()/onCcidCardInserted()に届くこと | interrupt IN endpointを持つCCIDリーダー＋カード | ✅ |
| [`vendor_bulk_out_only/`](vendor_bulk_out_only/) | bulk OUTのみでbulk INを持たない0xff interfaceを `vendorOpen()` が受け付け、packet sizeとendpoint channelの計上がdescriptorと一致すること。interface/endpointの一覧も出力する | USBグラフィックスアダプタ（DisplayLink DL-1xx、VID 0x17e9）またはbulk OUTのみのvendorデバイス | ✅ |
| [`vendor_bulk_throughput/`](vendor_bulk_throughput/) | bulk OUTの実効スループット。同期 `vendorWrite()` と非同期キュー（depth 1/2/4/8 × 転送サイズ512 B〜16 KB）を比較し、キューのスロット計上と再利用も検証する。実機のbulk OUT実効上限を確定させる（full-speed / ESP32-S3で1.098 MB/s、high speed / ESP32-P4で36.4 MB/s） | vendor-specific (0xff) のbulk OUT endpointを持つ任意のデバイス | ✅ |
| [`usb_display_dl1xx/`](usb_display_dl1xx/) | DL-1xxの立ち上げ。EDID読み出し、1920x1080のモード設定、単色塗り、カラーバー、1px市松、無通信での表示保持、モード再送を確認する。画像はモニタで目視判定する | USBグラフィックスアダプタ（DisplayLink DL-1xx、VID 0x17e9）＋1920x1080対応モニタ | ✅ |
| [`usb_display_turing/`](usb_display_turing/) | 3.5インチUSBスマートスクリーンの立ち上げ。CDC OUTキュー、向き、単色塗り、原色帯、カラーバー、1px市松、部分矩形、同じ全画面を1/3/8/24/48/96矩形に分けて送るスイープ、無通信での表示保持、輝度を確認する。画像はパネルで目視判定する | 3.5インチUSBスマートスクリーン（`1a86:5722`、`USB35INCHIPSV2`） | ✅ |
| [`usb_display_throughput/`](usb_display_throughput/) | 表示経路のチューニング計測。タイル形状、ダブルバッファ、差分転送、auto clear、全画面 vs sprite 再描画、panel直接描画（全クリアのちらつき含む）、シーン内容を振る。exampleのREADMEに書いた指針の出どころ。ターゲットごとに実行する（bus使用率は `--profile` で選ばれた実機の上限に対して計算される） | `usb_display_dl1xx` と同じ。ESP32-P4ではアダプタにセルフパワードハブが必要で、そのハブには他をつながないこと | ✅ |
| [`usbtmc_scpi/`](usbtmc_scpi/) | USBTMCの通し確認。vendor bulk APIでclass 0xfe / subclass 0x03のinterfaceを検出してclaim、`vendorControlTransfer()` によるEP0のGET_CAPABILITIESとCLEAR、`*IDN?`、設定値の書き込みと読み戻し、実測値、20回連続クエリ、接続中のCLEAR、SCPIエラーキューが空であること。機器の出力はONにしない | 菊水電子工業のPMXシリーズ直流電源（PMX18-5A `0b3e:1029` で開発） | ✅ |
| [`dp100/`](dp100/) | ALIENTEK DP100電源のHID通信。`onHIDInput()` と `sendHIDVendorOutput()` によるフレーム往復、DEVICE_INFO・BASIC_INFOのフィールドオフセット、mV / 0.1℃スケールを物理的な妥当範囲で検証、50回連続＋5回交互読み出しで拒否0。読み取り専用なので負荷を接続したままでも安全 | ALIENTEK DP100（`2e3c:af01`）をハブを介さず直結 | ✅ |
| [`dp100_output/`](dp100_output/) | ALIENTEK DP100の書き込み経路。0x20のindexフラグを持つBASIC_SETフレーム、出力ON/OFFとしてのstateバイト、設定値変更で保護しきい値が保たれること。書き込みの応答は信用しない（無視された書き込みにも成功が返る）ので、各段を設定値の読み戻しと出力の実測で確認する。**5.000V / 0.500Aで出力を投入するので、端子に何も接続しない状態で実行する。** 既に出力ONなら開始せず、元の設定値に復元する | ALIENTEK DP100（`2e3c:af01`）を直結、出力端子は未接続 | ✅ |
| [`printer_escpos/`](printer_escpos/) | USB Printerクラスの要求層。vendor bulk APIでclass 0x07のinterfaceを検出してclaim、`vendorControlTransfer()` によるEP0のGET_DEVICE_ID / GET_PORT_STATUS / SOFT_RESET、ESC/POSリアルタイムステータス4種の固定ビット確認、20回連続ポーリング、SOFT_RESET後もendpointが生きていること。**用紙を消費しない**（印字データを一切キューしない）。クラス要求に「中身の無い応答」を返すプリンタ（空のデバイスID、0x00のポートステータス）も許容する。XP-C58Kがまさにそれ | 用紙を装填したESC/POS USBレシートプリンタ（Xprinter XP-C58K `0483:070b` で開発） | ✅ |
| [`printer_print/`](printer_print/) | ESC/POS印字データ経路。レシート1枚を1転送で送り、プリンタのShift-JISフォントROMによる日本語、CODE128バーコード、QRコード、オートカッターまで確認する。印字前後のステータスと、転送後にステータス経路が生きていることも見る。**1回の実行で伝票1枚（約10cm）を消費し、用紙をカットする。** 用紙切れやエラーが報告されていれば印字せず、印字後に現れた場合は失敗にする。伝票の内容自体は目視判定 | `printer_escpos` と同じ。日本語には2バイトフォントROMを持つプリンタが必要 | ✅ |
| [`device_dump/`](device_dump/) | 全列挙デバイスのdescriptor・interface・endpoint・チャネル集計をダンプし、ライブラリ本体がclaimしないinterface（USBTMC・printer・vendor-specific）についてはラッパーが使うbulk/interrupt endpointも表示する。未対応デバイスの素性調査用 | 任意のUSBデバイス | ✅ |
| [`raw_descriptor/`](raw_descriptor/) | EP0への標準GET_DESCRIPTORでDEVICE/CONFIGURATIONディスクリプタの生バイトを読み、コンフィグレーションをブロック単位で走査して各ブロックを`bDescriptorType`付きで表示する。解析済みダンプでは見えないクラス固有ディスクリプタ（HID・CDC機能・CCID・UAC）も含む。USBPcapのキャプチャや`lsusb -v`と突き合わせるためのバイト列。コントロール転送の制限により248バイトで打ち切り | 任意のUSBデバイス | ✅ |
| [`hid_report_descriptor/`](hid_report_descriptor/) | 接続したHIDデバイスのHID report descriptorを取得して表示すること | USB HIDキーボードまたはマウス | — |

## ESP-IDF がクラッシュするハブの組み合わせ

一部のハブとデバイスの組み合わせは ESP-IDF ホストスタック自身のハブドライバを落とし、
本ライブラリ側で何をしても・何を避けても変わりません。リブートループに遭遇したときに
「調査済み」と分かるよう記録します。

| ハブ | 配下のデバイス | 結果 |
|---|---|---|
| CH335F（`1a86:8094`） | ALIENTEK DP100（`2e3c:af01`） | **リブートループ。** `assert failed: device_release ext_hub.c:509 (ext_hub_dev->dynamic.flags.waiting_release)`。経路は `ext_hub_process` → `handle_device` → `device_control_response_handling` → `handle_hub_descriptor` → `device_configure`。ESP-IDF v5.5.5 / arduino-esp32 3.3.11 |
| CH335F（`1a86:8094`） | 菊水 PMX18-5A、USBメモリ、HIDデバイス | 問題なし |
| RTD5411（`0bda:5411`） | ALIENTEK DP100 | 問題なし |
| —（直結） | ALIENTEK DP100 | 問題なし |

`probe/hub_enum` で確認した内容:

- DP100 を挿しても CH335F は**どのポートでも**接続を報告しない（4 ポートすべて
  `connected=0`、`powered=1`）。ポート電源を入れ直しても変わらない。落ちていないときも
  ハブがデバイスを認識していない
- **ライブラリは機構の一部ではない。** ハブ追跡を OFF にした状態、つまりハブに対する
  クライアントハンドルも hub descriptor / port status の通信も無い状態でも落ちる。
  assert 直前のログは `[phase1] tracked=0 hub_tracking=0 host_addresses=0` で、ハブが
  ホストスタックのアドレスリストに載る前に ext_hub 内部で落ちている
- 間欠的で、同じファームウェアが 2 回は無事に完走してから 1 回捕まえた

このループに入ったボードは書き込みが難しくなることがあります。そのため `probe/hub_enum`
は `setHubTrackingEnabled(false)` の状態で起動し、途中で追跡を有効化する構成にしてあり、
常に再書き込みできる状態で立ち上がります。

## ESP32-S3 の HCD チャネル制限

ESP32-S3 のUSBホストチャネル数は8個です（`OTG_NUM_HOST_CHAN`）。USBハブ経由で複数デバイスを接続すると、ESP-IDFがclaimするインターフェース分のホストチャネルを確保できずに失敗することがあります。実際の消費量はハブやデバイスのdescriptorに依存するため、このドキュメントにはこのテスト環境で実測した組み合わせだけを記録します。

ESP32-S3 + USBハブ経由の `multi_serial` 実測結果：

| 組み合わせ | 結果 | 備考 |
|------------|------|------|
| FTDI + CP210x | PASS | 両方のループバックが成功 |
| FTDI + CH34x | FAIL | HCDチャネル不足でendpoint allocationに失敗 |

ログに `No more HCD channels available`、`EP Alloc error: ESP_ERR_NOT_SUPPORTED`、`Claiming interface error: ESP_ERR_NOT_SUPPORTED` が出る場合、その構成ではESP32-S3のホストチャネル資源を超えています。1台ずつテストするか、別のハードウェア構成で実行してください。

## テスト結果

手動テストの結果は `--save-state` によって自動的に保存されます：

```
tests/.pytest-results/state.json
```

このファイルにはテストノードIDごとの最終実行結果とタイムスタンプが記録されます。機能を改修したときは、このファイルを参照して関連する手動テストの最終実行日時を確認し、再テストが必要かどうかを判断します。一度もパスしていないテストや、関連コードの変更より前にパスしたテストは再実行の対象と考えます。

なお、このファイルはマシンローカルなものでリポジトリにはコミットされません。削除されていたり、まだ一度も実行されていない場合もあります。また他の端末での実行結果は反映されません。あくまで目安として参照してください。

## 各カテゴリが手動である理由

| カテゴリ | 自動化できない理由 |
|---------|------------------|
| VCPシリアル（FTDI・CP210x・CH34x） | 実機のVCPハードウェアが必須。これらのベンダー固有プロトコルはESP32でエミュレートできない |
| 複数デバイス同時接続 | 複数の物理USBデバイスを同時に接続する必要がある |
| キーボードLEDの目視確認 | 合否判定は物理LEDの点灯状態に依存する |
| デバイスの活線挿抜ストレス | タイミングを合わせてケーブルを人が物理的に抜き差しする必要がある |
| USBハブ（情報表示・電源管理） | 実機のUSBハブが必須。自動テスト環境でもハブを経由して複数デバイスを接続することは技術的には可能だが、ハブの動作自体が試験対象に含まれるためノイズになる。そのため自動テストではハブを使わず1対1接続に限定している |
| USB Mass Storage | 実USBメモリごとのdescriptor、タイミング、SCSIコマンド応答差を確認する必要がある。peerテストだけでは実デバイス互換性を確認できない |
| USB Ethernet | 実USB NICごとのdescriptorとconfiguration構成が必要。vendor、CDC-ECM、CDC-NCM、任意のstorage configurationが混在する製品差はpeerテストでは再現できない |
| Android ADB | USBデバッグを有効にしたAndroid実機が必須。実機のdescriptor、ADB transport timing、認証状態はpeer deviceでは完全に再現できない |
| ハブのカスケード（ハブ下にハブ） | 2段以上ネストしたUSBハブが必須。物理的に用意できないためソフトウェアでエミュレートできない |
| 人間しか観測できない出力（オーディオ・MIDIなど） | 音声出力など、ソフトウェアから直接観測できない物理的な出力を伴う。オーディオループバック機器があれば自動化できるが、通常は人間による確認が必要 |

## 判定方法の方針

テストは、そのシナリオに合った最もシンプルな判定方法を使います。優先順位は以下の通りです：

1. **特殊デバイス、完全自動** — 実行前に必要なハードウェアを接続しておき、テスト本体は `dut.expect()` で完全自動実行。エミュレートできない実機が必要なためにmanualに分類。

2. **タイムアウト** — 「今デバイスを接続してください」などの指示を画面に表示し、タイムアウト付きでデバイス認識を待つ。y/n 入力不要。

3. **人間による目視確認** — 物理LED・音声出力などソフトウェアで観察できないものに限定。オペレーターが判断して `y` / `n` を入力する。

## テストの独立性

フラッシュ書き込みは `.py` ファイル単位で1回行われます。同じファイル内に複数のテスト関数がある場合、ボードはテスト間でリセットされないため、前のテストが残したデバイスの状態が後のテストに影響することがあります。

完全に独立させたい場合は、テストごとに独自の `.py` ファイルとスケッチに分離します。**1ファイルにつき1テスト関数**が基本的な推奨です。複数のテストを1ファイルにまとめるのは、意図的に状態を共有する設計で、その依存関係を許容できる場合に限ります。

## テストファイルのテンプレート

```python
"""
目的:
    このテストで何を検証するかを1文で記述する。

手動である理由:
    自動化できない具体的な理由。
    （例：「FTDIの実機が必須 — このVID/PIDはESP32でエミュレートできない」）

必要なハードウェア:
    - デバイスの種類と型番（例：FT232Rブレークアウトボード）
    - 接続方法（例：ESP32-S3ホストボードのUSBポートにUSB-Aで接続）

セットアップ手順:
    1. ホストボードにEspUsbHostUSBSerialスケッチを書き込む。
    2. VCPデバイスをホストボードのUSBポートに接続する。
    3. 実行: uv run --env-file .env pytest manual/<name>/<name>.py -v -s
"""

import pytest

# ---------------------------------------------------------------------------
# テスト
# ---------------------------------------------------------------------------

def test_something(dut):
    """
    合格の条件: <具体的に観察できる状態>
    不合格の条件: <失敗時に見える状態>
    """
    ...
```

### 期待される結果の書き方

著者以外のメンバーが読んでも合否を判断できる具体的な記述にします。

| 避けるべき書き方 | 代わりに書くこと |
|----------------|----------------|
| 「LEDが点灯するか確認する」 | 「`n` を送信してから1秒以内にNumLock LEDが点灯し、`0` を送信すると消灯する」 |
| 「データが受信されることを確認する」 | 「ホストから送信した64バイトが、デバイスのシリアル出力に同じ順序で現れる」 |
| 「クラッシュしないことを確認する」 | 「10回の接続・切断サイクル後もスケッチがシリアル出力を継続しており、エラーログが出ていない」 |
