# EspUsbHostDisplayDl1xx

English: [README.md](README.md)

DL-1xx系チップを搭載したUSBグラフィックスディスプレイアダプタを駆動し、LovyanGFXのpanelとして提供するサンプルです。

`.ino` には変更したくなる部分（何を描くか、どの解像度で動かすか）だけを置き、アダプタ固有の処理は隣のヘッダに分けてあります。そのまま他のプロジェクトへコピーして使えます。

| ファイル | 内容 |
|---|---|
| `Dl1xxProtocol.hpp` | `0xAF` bulkコマンド列。レジスタ書き込み、RLEピクセル書き込み、タイミングレジスタ用LFSR。Arduino / LovyanGFX / USBに非依存 |
| `Dl1xxModes.hpp` | 標準VESA/CEAタイミングとモード設定レジスタ列。同様にhostでコンパイル可能 |
| `Dl1xxDevice.hpp` | vendor interfaceのclaim、チャネル選択、EDID読み出し、モード設定、非同期bulk OUTキュー経由のピクセル送出 |
| `Panel_Dl1xx.hpp` | `lgfx::Panel_Device` の派生クラスと `LGFX_Dl1xx` |

## 必要なもの

- **LovyanGFX** と **LGFXVirtualCanvas 1.2.0 以降**（差分転送が入ったバージョン）。どちらも `sketch.yaml` に記載済みです。
- DisplayLink DL-1xx系チップ（VID `0x17e9`）搭載のUSBグラフィックスアダプタ。DL-120 / DL-160（"Alex"）とDL-115 / DL-125 / DL-165 / DL-195（"Ollie"）は同一プロトコルです。DL-165は1920x1080まで、DL-120 / DL-160は1600x1200程度が上限です。
- アダプタ用の電源。消費電流が大きいため、ホストボードのOTGコネクタから給電できない場合はセルフパワードハブか外部電源を使ってください。

## 仕組み

アダプタは自前のフレームバッファを持ち、USB無通信でもそこから走査を続けます。定期的なリフレッシュは不要です。したがってpanel側もフレームバッファを持たず、描画操作をそのままRLEピクセルコマンドに変換して送ります。

LGFXVirtualCanvasは画面を横帯（バンド）に分割し、1枚の小さなspriteを使い回して描画するため、Full HDの描画面でもホスト側にフルサイズのバッファが不要です（数十KBで足ります）。さらに差分転送で変化していないバンドの転送を省略でき、自前で絵を保持し続けるこのアダプタと相性が良いです。

ESP32-S3（full-speed USB）+ DL-165 実機での実測:

| | |
|---|---|
| フレームレート | 1920x1080 で 3 fps |
| 差分転送 | 1フレームあたり 2,073,600 px 中 215,040 px（10.4%）のみ送出 |
| USB転送量 | 約 42 KB/s。full-speedの実効上限 1.098 MB/s の約4% |

**律速はUSBではなく描画コールバック**です。LGFXVirtualCanvasはバンドごとに描画コールバックを再実行するためです。速度を上げたい場合は、バンドを大きくする（`setMemoryLimit()`）、`LGFXVirtualSprite` で変化部分だけ更新する、1フレームの描画内容を軽くする、といった描画側の手を打つことになります。

## 制限

- **回転は未対応**です。write経路がデバイスフレームバッファを線形にアドレスするため rotation 0 のみで、他の値は0に強制されます。
- **読み戻し不可**（`isReadable()` は false）。`readRect()`、ARGB合成、現在の画面内容を必要とする処理は動きません。
- `copyRect()` は何もしません。画面内矩形コピーには `AF 6A` コマンドが必要で、まだ実装していません。
- **16 bppのみ**です。チップは24 bppのデュアルプレーンモードも持っています。
- 同時に扱えるアダプタは1台です。
- モニタ側のHPDイベント（モニタの抜き差し、キャプチャ機器のクローズ）が起きると出力が黒くなり、ピクセル書き込みでは復帰しません。モードレジスタの再送で復帰します（`Dl1xxDevice::resendMode()`）。

## テスト

エンコーダのバグは実機では画面の乱れとしてしか見えないため、プロトコル層にはhost unit testを用意しています。

```sh
cd tests
uv run --env-file .env pytest unit/dl1xx -v -s
```

実機での立ち上げ確認（EDID、モード設定、単色塗り、カラーバー、市松、表示保持、モード再送）:

```sh
uv run --env-file .env pytest manual/usb_display_dl1xx/usb_display_dl1xx.py -v -s
```

設計、フェーズの内訳、実測値は [`docs/usb-display-spec.ja.md`](../../../docs/usb-display-spec.ja.md) にあります。

## プロトコルの参照元

実装は以下を参照してスクラッチで書いています。

- Florian Echtler による公開されたDL-1xxプロトコルのリバースエンジニアリング資料
- OpenBSD `sys/dev/usb/udl.c`（ISCライセンス）
- [Pico_USB_Disp](https://github.com/htlabnet/Pico_USB_Disp) の `docs/protocol.md`（MITライセンス）

GPL-2.0の `udlfb` ドライバ、LGPL-2.1の `libdlo`、およびSynapticsのDisplayLink SDKのコードは一切含みません。

DisplayLink is a trademark of Synaptics Incorporated. This project is not affiliated with, endorsed by, or certified by Synaptics.
