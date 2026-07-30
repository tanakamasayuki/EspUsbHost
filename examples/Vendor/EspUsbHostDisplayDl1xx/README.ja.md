# EspUsbHostDisplayDl1xx

English: [README.md](README.md)

DL-1xx系チップを搭載したUSBグラフィックスディスプレイアダプタを駆動し、LovyanGFXのpanelとして提供するサンプルです。

**作成中です。** 現時点ではプロトコル層のみが入っています。

| ファイル | 内容 | 状態 |
|---|---|---|
| `Dl1xxProtocol.hpp` | `0xAF` bulkコマンド列。レジスタ書き込み、RLEピクセル書き込み、タイミングレジスタ用LFSR | 完了・host test済み |
| `Dl1xxModes.hpp` | 標準VESA/CEAタイミングとモード設定レジスタ列 | 完了・host test済み |
| `Dl1xxDevice.hpp` | デバイス層。claim、チャネルキー、EDID、モード設定、ピクセル送出 | 未着手 |
| `Panel_Dl1xx.hpp` | `lgfx::Panel_Device` の派生クラス | 未着手 |
| `EspUsbHostDisplayDl1xx.ino` | サンプル本体 | 未着手 |

2つのヘッダはArduino・LovyanGFX・USBに依存しない作りにしてあるため、host上でコンパイルできます。`tests/unit/dl1xx` がg++でこれらをビルドし、LFSR、レジスタのバイト順、RLEエンコーダ（独立実装のデコーダとの照合）、Full HDのモード設定列を検証します。

```sh
cd tests
uv run --env-file .env pytest unit/dl1xx -v -s
```

設計、残りのフェーズ、スループット実測値は [`docs/usb-display-spec.ja.md`](../../../docs/usb-display-spec.ja.md) にあります。

## ハードウェア

DisplayLink DL-1xx系チップ（VID `0x17e9`）搭載のUSBグラフィックスアダプタが対象です。DL-120 / DL-160（"Alex"）とDL-115 / DL-125 / DL-165 / DL-195（"Ollie"）は同一プロトコルです。DL-165は1920x1080まで、DL-120 / DL-160は1600x1200程度が上限です。

これらのアダプタは消費電流が大きいため、ホストボードのOTGコネクタから給電できない場合はセルフパワードハブか外部電源を使ってください。

## プロトコルの参照元

実装は以下を参照してスクラッチで書いています。

- Florian Echtler による公開されたDL-1xxプロトコルのリバースエンジニアリング資料
- OpenBSD `sys/dev/usb/udl.c`（ISCライセンス）
- [Pico_USB_Disp](https://github.com/htlabnet/Pico_USB_Disp) の `docs/protocol.md`（MITライセンス）

GPL-2.0の `udlfb` ドライバ、LGPL-2.1の `libdlo`、およびSynapticsのDisplayLink SDKのコードは一切含みません。

DisplayLink is a trademark of Synaptics Incorporated. This project is not affiliated with, endorsed by, or certified by Synaptics.
