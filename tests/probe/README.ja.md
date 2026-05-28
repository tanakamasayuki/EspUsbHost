# Probe tests

ESP32-P4 の bring-up と USB ポート識別用の一時的な確認スケッチです。
正式な回帰テストではなく、ボード配線・接続先・PC 側認識に依存する初期切り分け用です。

`tests/` ディレクトリから個別に実行します。
プロファイル名は個別実行用の汎用 P4 として `esp32p4` を使います。

```sh
uv run --env-file .env pytest probe/p4_hs_host/p4_hs_host_probe.py
uv run --env-file .env pytest probe/p4_fs_host/p4_fs_host_probe.py
uv run --env-file .env pytest probe/p4_hs_device/p4_hs_device_probe.py
uv run --env-file .env pytest probe/p4_cdc/p4_cdc_probe.py
```

## ESP32-P4 probes

- `p4_hs_host` — HS OTG を USB Host として開始し、外部 USB デバイスの列挙を確認します。
- `p4_fs_host` — FS OTG を USB Host として開始し、外部 USB デバイスの列挙を確認します。
- `p4_hs_device` — HS device として HID keyboard + CDC composite の認識を確認します。
- `p4_cdc` — 素の `esp32p4` 設定で、疑っているコネクタが Hardware CDC/JTAG 側として COM 認識されるか確認します。

`p4_hs_device` と `p4_cdc` は PC 側のデバイスマネージャやシリアルモニターでの確認が必要です。
`p4_hs_host` と `p4_fs_host` は対象ポートに外部 USB デバイスを接続してから実行してください。

`.env` では `TEST_SERIAL_PORT_ESP32P4` に、この確認で使う P4 ボードのシリアルポートを設定してください。常時接続の `loopback/` 用 P4 は `p4_loopback` として別プロファイルにしています。
`p4_cdc` は意図的に `USBMode=hwcdc,CDCOnBoot=cdc` を付けていません。この設定を付けると `Serial` が Hardware CDC/JTAG に割り当たるため、ポート配線の素の状態を確認する用途には向きません。
