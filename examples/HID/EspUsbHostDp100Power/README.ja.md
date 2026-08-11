# EspUsbHostDp100Power

English: [README.md](README.md)

ALIENTEK（正点原子）の数控電源 DP100（`ATK-MDP100`、`2e3c:af01`）を USB 経由で
読み取る例。機器情報、入力電圧、出力電圧・電流、温度を取得する。

> **状態: 読み取りは動作確認済み（ESP32-S3）。** 以下の全フィールドは
> `tests/probe/dp100` で実機から実測し、`tests/manual/dp100` で検証している。
> 実測ログは [実測結果](#実測結果) にある。
> **設定書き込みフレームは実装済みだが未検証** —
> [書き込み側（未検証）](#書き込み側未検証) を参照。

| ファイル | 内容 |
|---|---|
| `Dp100Protocol.hpp` | ワイヤフォーマット。64 バイトのレポートフレーム、CRC-16/MODBUS、読み取り payload。Arduino / USB 依存なし |
| `Dp100Device.hpp` | HID interface の検出、interrupt OUT への要求送信、interrupt IN で返るレポートとの対応付け |
| `Dp100Power.hpp` | 意味づけ層。ボルト・アンペア・摂氏 |
| `EspUsbHostDp100Power.ino` | 接続 → 機器情報表示 → 実測値の定期取得 |

## なぜ `examples/HID/` にあるのか

DP100 は素の HID デバイスである。interface class `0x03`、subclass `0x00`、
protocol `0x00`、64 バイトの interrupt IN と 64 バイトの interrupt OUT を持ち、
独自プロトコルはそのレポートの中に載っている。

`examples/` のカテゴリは「デバイスが何をするか」ではなく **「ライブラリ本体のどの
API を使う example か」** で分かれている。これは HID API を使うので `HID/`。もう
一つの電源 example である [`EspUsbHostUsbtmcScpi`](../../Vendor/EspUsbHostUsbtmcScpi/)
は USBTMC を vendor bulk API で駆動するので `Vendor/` にある。**同じ種類の計測器
でもディレクトリが違うのは、軸が API だから**である。

## 仕組み

### ライブラリ側: 追加は不要

```cpp
// 要求: HID interrupt OUT へ生バイト。レポート ID は前置されない。
usb.sendHIDVendorOutput(report, 64, address);

// 応答: onHIDInput() が全 HID IN レポートを受け取る。
usb.onHIDInput([](const EspUsbHostHIDInput &input) { /* input.data をラッチ */ });
```

使うのは `onHIDInput()` で、**`onHIDVendorInput()` ではない**。後者はレポートの
1 バイト目が特定のレポート ID と一致したときだけ呼ばれるが、DP100 のフレームは
独自の方向マーカー（`0xfa`）から始まるので永久に一致しない。`onHIDInput()` は
その振り分けより前に呼ばれ、全件を受け取る。

ここから `Dp100Device.hpp` の設計が決まる。

- `onHIDInput()` は USB task 上で呼ばれるので、コールバックはレポートのコピーと
  フラグ立てだけを行う。待ち合わせは呼び出し側の task で行う
- `sendHIDVendorOutput()` は完了を待たないので、要求と応答の対応付けは example の
  責務。ラッチをクリア → 送信 → 「デコードでき、**かつ**要求した OpCode を持つ」
  レポートを待つ。別 OpCode のフレームは返さず捨てる。そうしないと対応付けが
  永久に 1 つ遅れたままになる

### フレーム

```
[dir][opcode][reserved][len][data ... ][crc lo][crc hi]   64 バイトまでゼロ埋め
```

- `dir` は Host→Device `0xfb`、Device→Host `0xfa`
- `crc` は **CRC-16/MODBUS**。byte 0 からデータ末尾までを対象に、リトルエンディアン
  で付ける
- 読み取り要求はデータを持たないので `len` は 0

したがって `DEVICE_INFO` 要求は次のようになる。

```
fb 10 00 00 <crc lo> <crc hi> 00 00 ... 00
```

**拒否されたフレームは無視ではなく応答が返る。** OpCode がエコーされ、本体は
1 バイトの `0x00` になる。同じ要求を誤った CRC 3 種で送ったところ、3 つとも
`fa 10 00 01 00` が返ったことで確定した。`isRefusal()` がこれを判定し、拒否応答を
payload として読まないようにしている。

### OpCode

| 値 | 名前 | ここでの扱い |
|---|---|---|
| 0x10 | DEVICE_INFO | 使用 |
| 0x30 | BASIC_INFO | 使用 |
| 0x35 | BASIC_SET | 実装済み・未検証 |
| 0x40 | SYSTEM_INFO | 使用（生バイト） |
| 0x45 | SYSTEM_SET | 未使用 |
| 0x50 / 0x55 | SCAN_OUT / SERIAL_OUT | 未使用 |
| 0x12–0x15 | ファームウェア更新 | 意図的に未実装 |

### payload

`DEVICE_INFO` は 40 バイト。

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

`BASIC_INFO` は 16 バイト。すべてリトルエンディアン 16 bit。

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

単位は仮定ではなく実測で決めた。約 12.16V の電源に対して入力が 12160、内部レールが
5067、室温約 29℃ に対して温度が 298 と 292。最大出力電圧は**現在の入力**で出せる
上限なので（12V 入力から 20V は出せない）、設定値の妥当性はこの値に対して確認する。
機種の 30V という定格ではない。

`SYSTEM_INFO` は 8 バイト（ここでは `50 00 1a 04 02 02 01 00`）。フィールドの意味は
確定していないので、推測せず生バイトのまま渡す。

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
拒否 0・フレームの取り違え 0 で完走している。

参考として descriptor（`tests/manual/device_dump`）:

```
VID:PID 2e3c:af01 class=0x00(per-interface)
Strings manufacturer="ALIENTEK" product="ATK-MDP100" serial="16A1C1C74000"
  Interface 0 alt=0 class=0x03(HID) subclass=0x00 protocol=0x00 endpoints=2 claimed=yes
    Endpoint iface=0 ep=0x81 dir=IN  type=interrupt max_packet=64 interval=1
    Endpoint iface=0 ep=0x01 dir=OUT type=interrupt max_packet=64 interval=1
```

## 書き込み側（未検証）

`BASIC_SET (0x35)` は電圧・電流の設定値、保護しきい値、**そして出力の ON/OFF を
1 フレームに載せている**。当てずっぽうで撃つと電源の出力が入りかねないので、
`tests/probe/dp100` はこれを送らず、`tests/manual/dp100` も一切触らない。
`Dp100Protocol.hpp` のレイアウトは公開されているリバースエンジニアリング実装の
記述に基づくもので、上記のフィールドと違い**本プロジェクトの実測では確認していない**。

スケッチでは `APPLY_SETPOINT`（既定 `false`）の後ろに置いてある。有効にする前に、
出力端子から負荷を外し、値を低く設定し、呼び出しごとに前面パネルを確認すること。
失敗した場合は「要求の形が未確定」であって「電源が壊れている」わけではない。

## 他デバイスへの流用

`Dp100Protocol.hpp` と `Dp100Device.hpp` は「64 バイト HID レポートの中に独自
フレームを載せる」という汎用の組み合わせで、多くの計測器やガジェットが同じ形を
とっている。別デバイスに向けるときに変わるのはプロトコル層のフレームヘッダと CRC
だけで、device 層の request/response 対応付けはそのまま使える。

## 参照

DP100 には一次情報のプロトコル文書が存在しない（ALIENTEK が配布しているのは
Windows 用 DLL `ATK-DP100DLL` だけ）。したがって **この実装の参照はデバイス自身**
であり、`tests/probe/dp100` で取得した実測が根拠である。以下の公開プロジェクトは
「何を探すか」を知るためと、フィールド順の裏取りに使った。

| 参照元 | ライセンス | 扱い |
|---|---|---|
| [ElluIFX/DP100-PyQt5-GUI](https://github.com/ElluIFX/DP100-PyQt5-GUI) | Unlicense | 参照した |
| [scottbez1/webdp100](https://github.com/scottbez1/webdp100) | Apache-2.0 | 参照した |
| [lessu/open_dp100](https://github.com/lessu/open_dp100) | LICENSE ファイルなし | 裏取りにのみ使用。転記はしていない |
| [weigu1/dp100_manipulator](https://github.com/weigu1/dp100_manipulator) | GPL-3.0 | **参照していない** |
| ALIENTEK の Windows DLL | プロプライエタリ | **逆解析していない** |

本ライブラリは MIT。GPL のコードは参照しておらず、ベンダー DLL の逆アセンブルも
行っていない。

> ALIENTEK and DP100 are trademarks of their respective owner. This project is not
> affiliated with, endorsed by, or certified by ALIENTEK.
