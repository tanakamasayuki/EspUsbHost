# Loopback テスト

> English: [README.md](README.md)

このディレクトリは、ESP32-P4 1台で USB Host と USB Device を同時に動かす
loopback テスト用の場所として残しています。

## ここにあるテスト

| テスト | 対象 | 必要な機材 |
|--------|------|-----------|
| [`p4_role_reversal/`](p4_role_reversal/) | ESP32-P4 の役割反転中の `usb.end()`。Host を full-speed 側、Device を high-speed 側で起動し、`device.end()` と `usb.end()` を連続で呼んで役割を入れ替えます。2.9.0 はこのテアダウン中に abort しました。その回帰テストです | 2つの USB コントローラを loopback 配線した ESP32-P4 が1台（`TEST_SERIAL_PORT_P4_LOOPBACK`）。2台目は不要です。device 側は `EspUsbDevice` で、リリース版を pin しています |

EspUsbDevice 側ではなくここに置いているのは、不具合がこのライブラリの `end()` にあり、
こちらのテストとして走らないリポジトリでは修正を検証できないためです。`EspUsbDevice` は
pin した依存として使うだけなので、実行にあたって兄弟リポジトリを取得・編集する必要はありません。

2つの条件が同時に成り立たないと再現しません。2枚構成の
[`probe/p4_end_teardown`](../probe/p4_end_teardown/) の ladder がこれを捕まえられないのはそのためです。
`end()` を呼ぶ時点で Host が **full-speed** 側にいること、そして直前に `device.end()` が呼ばれていて、
切断が片付く猶予がないままデバイスが抜けかけていることの2点です。

## loopback の大半をここに置かない理由

Arduino-ESP32 標準 USB Device 実装では ESP32-P4 の device 側制御に制限があり、
loopback 全般を Host 側の回帰テスト群として安定運用できないためです。

具体的には、ESP32-P4 では Arduino-ESP32 標準 USB Device 実装が HS 側でのみ動作します。
1台のP4で Host と Device を直結する loopback 構成では、Device が HS 側を使うため、
Host は FS 側に固定されます。この組み合わせでは HS device を FS host で処理できず、
endpoint の claim / allocation で失敗します。

これは loopback で同一チップ内の HS device と FS host を直結する場合だけの制約です。
USB Host 機能または Arduino-ESP32 標準 USB Device 機能を個別に利用する場合の一般的な問題ではありません。

ESP32-P4 loopback の実装と検証は、兄弟ライブラリ
[`EspUsbDevice`](https://github.com/tanakamasayuki/EspUsbDevice) 側で進めます。`EspUsbDevice` では
device port、speed、descriptor、endpoint MPS を明示的に制御し、released
`EspUsbHost` と組み合わせて詳細な Host / Device テストを行います。

このディレクトリへテストを戻す場合は、以下を満たすものだけにしてください。

- Arduino Core 標準 Device 実装だけで安定して再現できる Host 側回帰テスト。
- `EspUsbDevice` 側で見つかった Host 側不具合の最小再現で、このリポジトリに置く必要があるもの。
- README と `tests/TEST_PLAN*.md` に、実行条件と `EspUsbDevice` 側テストとの分担を追記したもの。
