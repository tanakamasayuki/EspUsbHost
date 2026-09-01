# Probe テスト

> English: [README.md](README.md)

bring-up と、デバイスのプロトコル解明用の一時的な確認スケッチです。
正式な回帰テストではなく、ボード配線・接続先・PC 側認識、あるいはプロトコルが未解明なデバイスに依存する初期切り分け用です。

`tests/` ディレクトリから個別に実行します。
プロファイル名は個別実行用の汎用 P4 として `esp32p4` を使います。

```sh
uv run --env-file .env pytest probe/p4_hs_host/p4_hs_host_probe.py
uv run --env-file .env pytest probe/p4_hs_fs_hub/p4_hs_fs_hub_probe.py -v -s
uv run --env-file .env pytest probe/p4_fs_host/p4_fs_host_probe.py
uv run --env-file .env pytest probe/p4_hs_device/p4_hs_device_probe.py
uv run --env-file .env pytest probe/p4_cdc/p4_cdc_probe.py
uv run --env-file .env pytest probe/rcs300_felica/rcs300_felica_probe.py -v -s
```

## ESP32-P4 probes

- `p4_hs_host` — HS OTG を USB Host として開始し、外部 USB デバイスの列挙を確認します。
- `p4_hs_fs_hub` — HS OTG の物理ポートを `HCFG.FSLSSUPP` でfull-speed専用にし、
  HS対応Hub自体がfull-speedで列挙され、その配下のFS/LSデバイスも列挙されることを確認します。
  詳細と判定基準は [`docs/p4-hs-port-fs-only-hub.ja.md`](../../docs/p4-hs-port-fs-only-hub.ja.md) を参照してください。
- `p4_fs_host` — FS OTG を USB Host として開始し、外部 USB デバイスの列挙を確認します。
- `p4_hs_device` — HS device として HID keyboard + CDC composite の認識を確認します。
- `p4_cdc` — 素の `esp32p4` 設定で、疑っているコネクタが Hardware CDC/JTAG 側として COM 認識されるか確認します。

`p4_hs_device` と `p4_cdc` は PC 側のデバイスマネージャやシリアルモニターでの確認が必要です。
`p4_hs_host` と `p4_fs_host` は対象ポートに外部 USB デバイスを接続してから実行してください。

`.env` では `TEST_SERIAL_PORT_ESP32P4` に、この確認で使う P4 ボードのシリアルポートを設定してください。このリポジトリでは現在 `loopback/` の実行用 P4 プロファイルは使いません。
`p4_cdc` は意図的に `USBMode=hwcdc,CDCOnBoot=cdc` を付けていません。この設定を付けると `Serial` が Hardware CDC/JTAG に割り当たるため、ポート配線の素の状態を確認する用途には向きません。

## ハブ・列挙の調査用 probe

- `hub_enum` — 直結では列挙されるのに特定のハブ経由では列挙されないデバイスを調べます。
  `DebugLevel=verbose` でビルドし、追跡中のデバイス、ホストスタック自身のアドレス一覧、
  各ハブのポート単位の接続状態を出し、続いて全ダウンストリームポートの電源を入れ直して
  もう一度出します。診断以外のことをしないので、「ハブがデバイスを認識できていない」のか
  「`printAllDeviceInfo()` のハブ問い合わせが列挙を壊した」のかも切り分けられます。
  ログ自体が出力なので `-s` を付けて実行します。

## リーダーのプロトコル解明用 probe

- `rcs300_felica` — Sony RC-S300 で System Code を指定した FeliCa Polling を行うための
  コマンド列を解明します。スケッチは単なるバイトポンプで、シリアルから hex 行で疑似 APDU を
  受け取り `PC_to_RDR_XfrBlock` または `PC_to_RDR_Escape` で送るだけなので、候補となる
  コマンド列はホスト側だけで差し替えられます(再フラッシュ不要)。解明した内容は probe の
  docstring と
  [`examples/Ccid/EspUsbHostCcidFelicaIdm`](../../examples/Ccid/EspUsbHostCcidFelicaIdm/)
  に記録しています。ログ自体が出力なので `-s` を付けて実行します。

- `dp100` — ALIENTEK DP100 電源が 64 バイトの HID レポートの中で期待するフレーム構造を
  解明します。スケッチは単なるバイトポンプで、シリアルから OpCode とデータを hex 行で
  受け取ってフレームを組み立て、CRC の変種を選べるので、フレーム形式はホスト側だけで
  探索できます(再フラッシュ不要)。デバイスの 1 バイト拒否応答ではなく本物の payload が
  返る CRC がどれか、で正解が分かります。読み取り専用で、設定値の OpCode は出力 ON/OFF を
  含むため意図的に一切送りません。解明した内容は probe の docstring と
  [`examples/HID/EspUsbHostDp100Power`](../../examples/HID/EspUsbHostDp100Power/)
  に記録しています。ログ自体が出力なので `-s` を付けて実行します。

- `printer_class` — ESC/POS プリンタが USB Printer クラスの要求のうちどれに実際に応答
  するのかを解明します。`manual/printer_escpos` で GET_DEVICE_ID が失敗し、
  GET_PORT_STATUS が「非選択・エラー」とデコードされる値を返す一方でプリンタは明らかに
  正常でした。しかもホスト側の間違いは未実装と全く同じに見えます(wValue は
  コンフィグレーションインデックス、wIndex は interface を**上位**バイトに置く、という
  このクラスの他の要求と異なる形式のため)。スケッチは両フィールドに加えて
  device recipient 形式・vendor type 形式も総当たりし、ポートステータスは他のやり取りの
  前と SOFT_RESET の後にも読みます。結論は「spec どおりの形式だけが受け付けられ、
  プリンタには言うことが無い(空のデバイス ID と 0x00 のステータス)」でした。だから
  example はどちらも失敗として扱いません。用紙は消費しません。解明した内容は probe の
  docstring と
  [`examples/Vendor/EspUsbHostPrinterEscPos`](../../examples/Vendor/EspUsbHostPrinterEscPos/)
  に記録しています。ログ自体が出力なので `-s` を付けて実行します。

`rcs300_felica` を実行する前にリーダーへ FeliCa カードを載せてください。ポートは
`TEST_SERIAL_PORT_ESP32S3` を使います。
