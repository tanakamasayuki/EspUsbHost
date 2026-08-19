# EspUsbHostDp100Power

> English: [README.md](README.md)

ALIENTEK（正点原子）の数控電源 DP100（`ATK-MDP100`、`2e3c:af01`）を USB 経由で
操作するサンプルです。機器情報・入力電圧・出力電圧電流・温度の読み取りと、電圧・
電流制限・出力 ON/OFF の設定を行います。

> **状態: 動作確認済み（ESP32-S3）。** 以下の全フィールドとフラグは
> `tests/probe/dp100` で実機から実測し、`tests/manual/dp100`（読み取り）と
> `tests/manual/dp100_output`（設定値と出力 ON/OFF）で検証しています。
> 実測ログは [実測結果](#実測結果) にあります。

| ファイル | 内容 |
|---|---|
| `Dp100Protocol.hpp` | ワイヤフォーマット。64 バイトのレポートフレーム、CRC-16/MODBUS、読み取り payload。Arduino / USB 依存なし |
| `Dp100Device.hpp` | HID interface の検出、interrupt OUT への要求送信、interrupt IN で返るレポートとの対応付け |
| `Dp100Power.hpp` | 意味づけ層。ボルト・アンペア・摂氏と設定系 API |
| `EspUsbHostDp100Power.ino` | 接続 → 機器情報と設定値の表示 → 実測値の定期取得 |

## なぜ `examples/HID/` にあるのか

DP100 は素の HID デバイスです。interface class `0x03`、subclass `0x00`、
protocol `0x00`、64 バイトの interrupt IN と 64 バイトの interrupt OUT を持ち、
独自プロトコルはそのレポートの中に載っています。

`examples/` のカテゴリは「デバイスが何をするか」ではなく **「ライブラリ本体のどの
API を使う example か」** で分かれています。これは HID API を使うので `HID/` に
あります。もう一つの電源 example である [`EspUsbHostUsbtmcScpi`](../../Vendor/EspUsbHostUsbtmcScpi/)
は USBTMC を vendor bulk API で駆動するので `Vendor/` にあります。**同じ種類の
計測器でもディレクトリが違うのは、軸が API だから**です。

## 仕組み

### ライブラリ側: 追加は不要

```cpp
// 要求: HID interrupt OUT へ生バイト。レポート ID は前置されない。
usb.sendHIDVendorOutput(report, 64, address);

// 応答: onHIDInput() が全 HID IN レポートを受け取る。
usb.onHIDInput([](const EspUsbHostHIDInput &input) { /* input.data をラッチ */ });
```

使うのは `onHIDInput()` で、**`onHIDVendorInput()` ではありません**。後者はレポートの
1 バイト目が特定のレポート ID と一致したときだけ呼ばれますが、DP100 のフレームは
独自の方向マーカー（`0xfa`）から始まるので永久に一致しません。`onHIDInput()` は
その振り分けより前に呼ばれ、全件を受け取ります。

ここから `Dp100Device.hpp` の設計が決まります。

- `onHIDInput()` は USB task 上で呼ばれるので、コールバックはレポートのコピーと
  フラグ立てだけを行います。待ち合わせは呼び出し側の task で行います
- `sendHIDVendorOutput()` は完了を待たないので、要求と応答の対応付けは example の
  責務です。ラッチをクリア → 送信 → 「デコードでき、**かつ**要求した OpCode を持つ」
  レポートを待ちます。別 OpCode のフレームは返さず捨てます。そうしないと対応付けが
  永久に 1 つ遅れたままになります

### フレーム

```
[dir][opcode][reserved][len][data ... ][crc lo][crc hi]   64 バイトまでゼロ埋め
```

- `dir` は Host→Device `0xfb`、Device→Host `0xfa`
- `crc` は **CRC-16/MODBUS**。byte 0 からデータ末尾までを対象に、リトルエンディアン
  で付けます
- 読み取り要求はデータを持たないので `len` は 0 です

したがって `DEVICE_INFO` 要求は次のようになります。

```
fb 10 00 00 <crc lo> <crc hi> 00 00 ... 00
```

**1 バイトの本体は payload ではなくステータスコードです。** `0x01` が成功、`0x00` が
失敗です。同じ要求を誤った CRC 3 種で送ったところ 3 つとも `fa 10 00 01 00` が
返り、後に受理された `BASIC_SET` が `fa 35 00 01 01` を返したことで確定しました。
`isSuccess()` / `isFailure()` で判別し、ステータスを payload として読まないように
しています。

### OpCode

| 値 | 名前 | ここでの扱い |
|---|---|---|
| 0x10 | DEVICE_INFO | 使用 |
| 0x30 | BASIC_INFO | 使用 |
| 0x35 | BASIC_SET | 使用（設定値と出力 ON/OFF） |
| 0x40 | SYSTEM_INFO | 使用（生バイト） |
| 0x45 | SYSTEM_SET | 未使用 |
| 0x50 / 0x55 | SCAN_OUT / SERIAL_OUT | 未使用 |
| 0x12–0x15 | ファームウェア更新 | 意図的に未実装 |

### payload

`DEVICE_INFO` は 40 バイトです。

| offset | size | フィールド |
|---|---|---|
| 0 | 16 | 機種名、`0xff` パディング — `"ATK-DP100"`。USB の product 文字列 `"ATK-MDP100"` とは**別物** |
| 16 | 2 | ハードウェアバージョン |
| 18 | 2 | アプリケーションバージョン |
| 20 | 2 | boot バージョン |
| 22 | 2 | run area |
| 24 | 12 | シリアル（バイト列）— USB の serial 文字列とも**別物** |
| 36 | 2 | 年 |
| 38 | 1 | 月 |
| 39 | 1 | 日 |

`BASIC_INFO` は 16 バイトです。すべてリトルエンディアン 16 bit です。

| offset | フィールド | 単位 |
|---|---|---|
| 0 | 入力電圧 | mV |
| 2 | 出力電圧 | mV |
| 4 | 出力電流 | mA |
| 6 | 現在の入力で出せる最大出力電圧 | mV |
| 8 | 温度 1 | 0.1 ℃ |
| 10 | 温度 2 | 0.1 ℃ |
| 12 | 内部 5V レール | mV |
| 14 | 出力モード（1 バイト）、動作状態（1 バイト） | |

単位は仮定ではなく実測で決めました。約 12.16V の電源に対して入力が 12160、内部レールが
5067、室温約 29℃ に対して温度が 298 と 292 でした。最大出力電圧は**現在の入力**で出せる
上限なので（12V 入力から 20V は出せません）、設定値の妥当性はこの値に対して確認します。
機種の 30V という定格ではありません。

`SYSTEM_INFO` は 8 バイト（ここでは `50 00 1a 04 02 02 01 00`）です。フィールドの意味は
確定していないので、推測せず生バイトのまま渡します。

## 実測結果

`tests/manual/dp100`（ESP32-S3、DP100 を直結）:

```
connected address=1 vid=2e3c pid=af01 product="ATK-MDP100" serial="16A1C1C74000"
dp100 interface address=1 interface=0
device type="ATK-DP100" hw=14 app=14 boot=11 run_area=0x00aa built=2024-12-02 serial=c7819d000040041622a75005
basic in=12.160V out=0.000V 0.000A max_out=11.800V rail5v=5.067V temp=29.6/29.0C mode=2 status=0
system info raw=50001a0402020100 len=8
repeated reads done refusals=0 received=53
interleaved reads done
```

50 回の連続読み出しと、`DEVICE_INFO` / `BASIC_INFO` を交互に 5 ラウンド行って、
拒否 0・フレームの取り違え 0 で完走しています。

`tests/manual/dp100_output`（出力端子に何も接続しない状態）:

```
original setpoint index=0 state=0x00 4000mV 3000mA ovp=30500mV ocp=5050mA
before: out 0.000V 0.000A
after write: setpoint 5000mV 500mA state=0x00
after setpoint write: out 0.000V 0.000A
output on: out 4.998V 0.000A mode=1 status=0
output off: out 0.000V 0.000A
restored: 4000mV 3000mA state=0x00
final: out 0.000V 0.000A
refusals=0 received=15
```

設定値の書き込みは出力を触らず、`state` バイトが出力を切り替えます。4.998V は
電源自身が自分の端子を測った値で、**書き込みが効いたことの唯一の証拠**です。

参考として descriptor（`tests/manual/device_dump`）:

```
VID:PID 2e3c:af01 class=0x00(per-interface)
Strings manufacturer="ALIENTEK" product="ATK-MDP100" serial="16A1C1C74000"
  Interface 0 alt=0 class=0x03(HID) subclass=0x00 protocol=0x00 endpoints=2 claimed=yes
    Endpoint iface=0 ep=0x81 dir=IN  type=interrupt max_packet=64 interval=1
    Endpoint iface=0 ep=0x01 dir=OUT type=interrupt max_packet=64 interval=1
```

## 書き込み側

`BASIC_SET (0x35)` は電圧・電流の設定値、保護しきい値、**そして出力の ON/OFF を
1 フレームに載せています**。だから 1 項目だけ変えたい場合も「読んで、直して、書き戻す」
必要があり、`Dp100Power::setVoltage()` などはそうしています。

| offset | フィールド |
|---|---|
| 0 | index（**フラグ付き**） |
| 1 | state — 出力 ON/OFF: 1 で ON、0 で OFF |
| 2 | 電圧設定値 mV |
| 4 | 電流設定値 mA |
| 6 | 過電圧しきい値 mV |
| 8 | 過電流しきい値 mA |

**index のフラグが最大の勘所で、間違えても何も言われません。**

- 読み出しは `index | 0x80`
- 書き込みは `index | 0x20`

素の index や `index | 0x80`、`| 0xa0` への書き込みは**成功ステータスを返した上で
完全に無視されます**。応答から区別する手段はありません。だから `Dp100Device` はステータスを
信用せず読み戻し、manual テストは各段を `BASIC_INFO` で確認しています。実機で index と
state を 3 巡スイープしても何も起きず、`0x20` を試して初めて動きました。

index はプリセット群で、index 0 が電源が実際に使う設定です。この DP100 の初期値は
0 = 4.000V / 3.000A、1 = 2.000V / 1.000A、2 = 3.000V / 1.500A、3 = 4.000V / 2.000A、
いずれも OVP 30.5V / OCP 5.05A でした。

スケッチでは書き込みが `APPLY_SETPOINT`、出力 ON が `TURN_OUTPUT_ON` の後ろにあり、
どちらも既定で `false` です。電源に対する物理的な操作なので、出力端子に何が繋がって
いるかを先に確認してください。

## 他デバイスへの流用

`Dp100Protocol.hpp` と `Dp100Device.hpp` は「64 バイト HID レポートの中に独自
フレームを載せる」という汎用の組み合わせで、多くの計測器やガジェットが同じ形を
とっています。別デバイスに向けるときに変わるのはプロトコル層のフレームヘッダと CRC
だけで、device 層の request/response 対応付けはそのまま使えます。

## 参照

DP100 には一次情報のプロトコル文書が存在しません（ALIENTEK が配布しているのは
Windows 用 DLL `ATK-DP100DLL` だけです）。したがって **この実装の参照はデバイス自身**
であり、`tests/probe/dp100` で取得した実測が根拠です。以下の公開プロジェクトは
「何を探すか」を知るためと、フィールド順の裏取りに使いました。

プロトコル解析メモの全文は
[docs/dp100-spec.ja.md](../../../docs/dp100-spec.ja.md) にあります。

| 参照元 | ライセンス | 扱い |
|---|---|---|
| [ElluIFX/DP100-PyQt5-GUI](https://github.com/ElluIFX/DP100-PyQt5-GUI) | Unlicense | 参照しました |
| [scottbez1/webdp100](https://github.com/scottbez1/webdp100) | Apache-2.0 | 参照しました。`0x20` の書き込みフラグはここで判明しました（実機で index / state をスイープしても分かりませんでした）。このビットの意味が不明という点も同じです |
| [lessu/open_dp100](https://github.com/lessu/open_dp100) | LICENSE ファイルなし | 裏取りにのみ使用。転記はしていません |
| [weigu1/dp100_manipulator](https://github.com/weigu1/dp100_manipulator) | GPL-3.0 | **参照していません** |
| ALIENTEK の Windows DLL | プロプライエタリ | **逆解析していません** |

本ライブラリは MIT です。GPL のコードは参照しておらず、ベンダー DLL の逆アセンブルも
行っていません。

> ALIENTEK and DP100 are trademarks of their respective owner. This project is not
> affiliated with, endorsed by, or certified by ALIENTEK.
