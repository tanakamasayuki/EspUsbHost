# テスト計画

> English: [TEST_PLAN.md](TEST_PLAN.md)

## テスト方針

テストはソフトウェアで環境を完全に制御できるかどうかに基づいて2種類に分けます。

**自動テスト**はCIまたはローカルで人の操作なしに実行します。
入力はすべてプログラムで生成し、期待される出力はすべてアサーションで検証します。

**手動テスト**は環境をソフトウェアで完全に制御できない場合に使います。
「自動化が面倒だから」ではなく、エミュレートできない物理ハードウェアが必須である、または人の判断（目視確認・触覚フィードバックなど）による検証が本質的に必要である場合に限ります。

### EspUsbDevice とのテスト分担

`EspUsbHost` 側の `peer/` テストは、原則として Arduino-ESP32 標準 USB Device 実装を使った
現在の構成を維持します。Host ライブラリが兄弟ライブラリ `EspUsbDevice` だけに過適応することを避け、
Arduino Core 標準 Device 実装との組み合わせで基本的な相互運用性を確認するためです。

`EspUsbDevice` 側では、このリポジトリの released `EspUsbHost` と組み合わせて、descriptor、
report ID、output / feature report、複合 HID、CDC、MIDI、MSC など、Arduino Core 標準 Device
実装では制御しづらい項目を詳しく検証します。ESP32-P4 1台構成の loopback テストも、現在は
`EspUsbDevice` 側を主な整備場所にします。

ESP32-P4 loopback をこのリポジトリ側で維持しない理由は、Arduino-ESP32 標準 USB Device 実装が
P4 では HS 側でのみ動作するためです。1台構成で直結すると Device が HS 側を使い、Host は FS 側に
固定されますが、FS host では HS device を処理できず endpoint claim / allocation が失敗します。
これは loopback 直結時だけの制約であり、USB Host または標準 USB Device 機能を個別に使う場合の
一般的な問題ではありません。

Host 側の未リリース修正を `EspUsbDevice` で先行確認する場合は、切り分け目的でローカル checkout
を使ってよいですが、通常の合格条件は released `EspUsbHost` と Arduino Core 標準 Device 実装の
組み合わせを基準にします。

`peer/` で `EspUsbDevice` peer を使うのは、Arduino Core 標準 device stack ではそのデバイスを
表現できない場合だけです（`usb_vendor`、`usb_ncm`、`usb_ncm_throughput`、`hid_keyboard_composite`、
`hid_keyboard_nkro`、`usb_audio_uac2`）。UAC2 はまさにこのケースで、`USBAudioCard` が UAC1 専用の
ため、Host 側の UAC2 実装が扱う Clock Source entity・4 バイトの Feature Unit control・`RANGE`
リクエストを Arduino Core からは提示できません。

```
tests/
  harness/    自動 — テスト機構そのもののテスト。実機不要
  peer/       自動 — ESP32-S3 2台構成（1台ホスト + 1台デバイス）
  loopback/   予約 — ESP32-P4 1台構成用。現在このリポジトリには実行可能テストなし
  manual/     手動 — 特殊ハードウェアまたは人による操作が必要
  probe/      プローブ — bring-upとプロトコル解析用の使い捨てスケッチ
  unit/       自動 — ホスト上のg++テスト。ボード不要
```

ハードウェアの接続方法や個別テストの詳細は、各サブディレクトリの README を参照してください。

---

## テストの構成

**1モジュール1テスト。** モジュールはスケッチのディレクトリ1つで、実行にはコンパイルとアップロードがかかる。
モジュール内のテスト1件はほとんど費用がかからない。この環境での実測はモジュールの固定費が約29秒、
モジュール内のテスト1件が約0.6秒。したがって支払う価値のある粒度はモジュールであり、
本来なら複数テストにするものは1テスト内の**チェック**として書く。

チェックはただの入れ子関数で、`run_checks`（`tests/conftest.py` のフィクスチャ）が順に呼ぶ。
失敗は普通に伝播するので、pytest のトレースバックが失敗したフレーム名＝チェック名を示し、
落ちた行がそのまま出る。

```python
def test_usb_msc(dut, peers, run_checks):
    def capacity():
        dut.write("c")
        dut.expect_exact("MSC_CAPACITY ok=1 blocks=16 block_size=512")

    def inquiry():
        dut.write("i")
        dut.expect_exact("MSC_INQUIRY ok=1 removable=1 vendor='ESP32' ...")

    run_checks([capacity, inquiry])
```

これは規則に合わせるための妥協ではない。`usb_msc` は20テストで46.8秒、1テストで31.0秒だった。
pytest のテスト単位の固定費を20回ではなく1回しか払わないため。

**例外は2つ。どちらも「ケース単位で何かを付ける必要がある」場合。**

- `usb_midi` は `test_usb_midi_lifecycle_listeners_on_peer_reboot` を独立したテストのまま残す。
  `tests/conftest.py` がそのノードIDに限って1行のシリアル出力を許可しているため。
  統合すると許可がモジュール全体に広がり、許すべきでない場所で同じ行が見逃される。
- 破壊的なチェックは、テストの並び順で最後に置くのではなく専用モジュールにする。
  同じスケッチディレクトリに2つ目のテストファイルを置けば2つ目のモジュールになり、
  そのアップロードがボードを復帰させる。現時点で該当するものはない。

**チェックの順序が結果を変えてはならない。** テストを1つに統合することは順序依存を
除去するのではなくテストの内側に移すだけなので、監査も一緒に内側へ移す。

```bash
ESPUSBHOST_REVERSE_CHECKS=1 pytest peer/
```

全モジュールのチェックが逆順で走る。片方の順序でしか通らないチェックは設計の誤りで、
前のチェックが残した状態に頼るのではなく、必要な状態を自分で作る。
逆順の実行にアップロードの追加は要らないので、リリース前ではなく日常の検査として使える。

**ホストは起動時に開始しない。** peer 基板は DUT が起動した後に書き込まれるため、
既に動いているホストはそのリセットを観測し、そこで起きた列挙をエラーとして記録してしまう。
そこで `setup()` は `HOST_STATE idle devices=0` を出すだけにして、
autouse フィクスチャが `G` で開始し `H` で停止する。

このフィクスチャには効いている点が2つある。`peers` を要求しているのは**順序のためだけ**で、
これが無いと autouse フィクスチャが peer の書き込みより先に走り、
ゲートが閉じようとしている窓の中にホストを戻してしまう。
また `G` はスケッチ側で冪等なので、開始はモジュールに1回で済む。
テスト毎に払うとスイートは12分33秒から21分49秒に伸び、モジュール毎に1回なら
ゲート無しに対して47秒しか増えない。

**準備完了は問い合わせる。待ち受けない。** `HOST_CONNECTED` などは
デバイスが列挙されたときに一度だけ出力されるので、それを待つチェックは
たまたま最初に走るときしか動かない。ゲート化された全スケッチは `Q` に現在の状態を答えるので、
チェックはそれをポーリングする。

## テストカバレッジ一覧

| 機能 | 自動 | 手動 | 未カバー |
|------|------|------|---------|
| HIDキーボード入力 | ✅ peer（通常文字列、Shift modifier付きboot report） | | |
| HIDキーボードレイアウト（JP） | ✅ peer（`Shift+International3`）、✅ hid_logic | | |
| HIDマウス入力 | ✅ peer | | |
| HIDコンシューマーコントロール | ✅ peer | | |
| HIDシステムコントロール | ✅ peer | | |
| HIDゲームパッド | ✅ peer | | |
| HID複数listener配送 | ✅ peer（単一callbackとの共存、listener単独配送、順序、上限、解除、callback内変更） | | |
| HIDベンダー入出力 | ✅ peer | | |
| HID生データダンプ | ✅ peer (custom_hid) | | |
| キーボードLED出力 | ✅ peer (hid_logic) | ✅ manual（目視） | |
| USBシリアル — CDC ACM | ✅ peer、line coding設定、`end()`/再開 | | |
| USBシリアル — VCP（FTDI・CP210x・CH34x） | | ✅ manual、シリアル形式設定 | |
| USB MIDI | ✅ peer | | |
| MIDI複数listener配送 | ✅ peer（単一callbackとの共存、listener単独配送、順序、上限、解除、callback内変更） | | |
| device lifecycle複数listener配送 | ✅ peer（`end()`/再開による接続event、peer再起動による切断→再接続、単一callbackとの共存、順序、専用上限8、解除） | | |
| Vendor-specific bulk/control | ✅ peer（usb_vendor、`end()`/再開を含む） | ✅ manual（Android ADB認証＋shell stream） | |
| USBオーディオ入出力 — UAC1 | ✅ peer（標準`USBAudioCard`で双方向） | | ⬜ 実USBマイク・オーディオIF |
| USBオーディオ入出力 — UAC2 | ✅ peer（`usb_audio_uac2`: class revision、Clock Sourceのサンプルレート、4バイト・2ビットのFeature Unit control、volumeの`RANGE`、explicit feedback endpointのポーリングとOUTのレート追従、双方向streaming）、✅ ホスト単体（`unit/audio_uac`: descriptorと`RANGE`のデコード） | | ⬜ 実UAC2機器（high-speed設計が多く、full-speedホストでは列挙できない）、Clock Selector / Clock Multiplier、実DACでの長時間の非同期playback（peerのfeedbackはFIFO残量からの計算でハードウェアクロック由来ではない） |
| USB Mass Storage — ブロックI/O / FatFsマウント | ✅ peer（容量、Inquiry/Sense、read/write、範囲外拒否、write失敗検出、デバイス接続中とdevice listが空の状態での`end()`/再開。usb_msc_fat: peerが整形したFAT12ボリュームのmount/read/unmount、mount→unmount→`end()`→`begin()`→再mount、マウント中`end()`でのボリューム解放） | ✅ manual（実USBメモリの容量取得、LBA0 read、FatFs/VFS mount、`fs::FS` wrapper、ファイルwrite/read/delete、mount中disconnect/remount） | ⬜ data phase失敗後の完全なBOT復旧、複数LUN、32-bit sector超のFatFs mount |
| CCIDスマートカードリーダー | | ✅ manual（`ccid_info` のdescriptorダンプ、`ccid_card` のopen/状態/ATR/APDU、`ccid_hotplug` のslot変化通知をSony RC-S300で確認） | ⬜ 複数slotリーダー、接触カード、チェイン応答、ICCD変種 |
| USBTMC — SCPI計測器 | | ✅ manual（`usbtmc_scpi`: class 0xfeのinterface claim、EP0のGET_CAPABILITIESとCLEAR、`*IDN?`、設定値の読み戻し、実測値、連続クエリ、菊水PMX18-5Aでエラーキューが空）、✅ host unit（`unit/usbtmc`: メッセージヘッダ、bTag規則、同期ずれの拒否、capabilityオフセット） | ⬜ interrupt INによるUSB488 service request、実機でのABORT_BULK_IN/OUT回復、PMX電源以外の計測器 |
| Printer — ESC/POSレシートプリンタ | | ✅ manual（`printer_escpos`: class 0x07のinterface claim、EP0のGET_DEVICE_ID / GET_PORT_STATUS / SOFT_RESET、リアルタイムステータス4種、20回連続ポーリング、SOFT_RESET後もendpointが生存。用紙を消費しない）、✅ manual（`printer_print`: レシート1枚を1転送で印字。日本語Shift-JIS・CODE128バーコード・QRコード・オートカットを伝票で目視確認、印字前後のステータス確認）、✅ host unit（`unit/escpos`: クラス要求、デバイスID解析、ポートステータスのビット、全ESC/POSコマンドのバイト単位一致、ビルダのオーバーフロー）、🔍 probe（`probe/printer_class` でこの機種がクラス要求に中身の無い応答を返すことを確定） | ⬜ 実機での用紙切れ・カッタージャム経路、単方向プリンタ（protocol 0x01）、80mm紙幅、IPP / PWG-Raster / PCL、IEEE 1284.4パケットモード（対象外） |
| ALIENTEK DP100電源（HID独自フレーム） | | ✅ manual（`dp100`: HID APIでのフレーム往復、DEVICE_INFO / BASIC_INFOのオフセットと単位、連続・交互読み出し）、✅ host unit（`unit/dp100`: CRC-16/MODBUS、フレームのencode/decode、実機キャプチャの回帰）、✅ manual（`dp100_output`: 0x20のindexフラグを持つBASIC_SET、端子で実測した出力ON/OFF、保護しきい値の保持、元設定への復元）、🔍 probe（`probe/dp100` でフレーム・CRC・両indexフラグを確定） | ⬜ SYSTEM_INFOのフィールド意味、SYSTEM_SET、SCAN_OUT / SERIAL_OUT、ファームウェア更新（対象外） |
| USB Ethernet — CDC-ECM/CDC-NCM | | ✅ manual（configuration横断の汎用descriptor候補検出） | ⬜ configuration選択、frame RX/TX、lwIP統合 |
| 複数デバイス同時接続 | | ✅ manual | |
| デバイス活線挿抜 | | ✅ manual | |
| HUB検出・トポロジー・ポート電源制御 | | ✅ manual（`hub_info`、`hub_power`） | ⬜ change bit clear、複数段Hub、USB 3.x Hub互換性 |
