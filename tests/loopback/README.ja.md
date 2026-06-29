# Loopback テスト

> English: [README.md](README.md)

このディレクトリは、ESP32-P4 1台で USB Host と USB Device を同時に動かす
loopback テスト用の場所として残しています。

現在、このリポジトリ側には実行可能な loopback テストは置いていません。
Arduino-ESP32 標準 USB Device 実装では ESP32-P4 の device 側制御に制限があり、
Host 側の回帰テストとして安定運用できないためです。

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
