# EspUsbHost ドキュメント索引

> English: [README.md](README.md)

`docs/` 以下の全ドキュメントを、探しているものの種類ごとに並べています。ライブラリのAPI仕様そのものはトップレベルの [README.ja.md](../README.ja.md) にあります。

## ガイド

| ドキュメント | 内容 |
|-------------|------|
| [usb-host-guide.ja.md](usb-host-guide.ja.md) · [en](usb-host-guide.md) | **まずここから。** USB Hostの基礎、電源とハブ、ESP32固有の制限、実験を進める順序、未知プロトコルのキャプチャと解析、トラブルシューティング |
| [usb-host-advanced.ja.md](usb-host-advanced.ja.md) · [en](usb-host-advanced.md) | アーキテクチャとタスクモデル、ディスクリプタのバイト構造、コントロール転送の解剖、タイミングと帯域、チャネルとFIFO分割、エラー復帰、スループット設計、コールバックのコンテキスト、新しいクラスの実装 |
| [tested-devices.ja.md](tested-devices.ja.md) · [en](tested-devices.md) | 実機で確認したデバイスとボードの一覧。VID:PID、条件、確認済み／未確認の範囲 |
| [usb-display.ja.md](usb-display.ja.md) · [en](usb-display.md) | USBディスプレイ系サンプルの索引。3種類の異なるプロトコルと転送方式 |

## プロトコル解析メモ

プロトコルを自力で解析する必要があった機器について、対応するサンプルの実装中に書いたメモです。自分で[プロトコルを解析する](usb-host-guide.ja.md#5-プロトコルの解析)ときの型としても使えます。**日本語で書かれています**（英語版があるものは下表に併記）。各ファイルの冒頭には、内容と現行の英語ドキュメントの場所を示す英語の案内を付けています。

| ドキュメント | 機器・プロトコル | 対応サンプル |
|-------------|----------------|-------------|
| [vendor-api-spec.ja.md](vendor-api-spec.ja.md) | vendor bulk/control APIの設計 | [`Vendor`](../examples/Vendor/) |
| [printer-spec.ja.md](printer-spec.ja.md) | USB PrinterクラスとESC/POS | [`EspUsbHostPrinterEscPos`](../examples/Vendor/EspUsbHostPrinterEscPos/) |
| [usbtmc-spec.ja.md](usbtmc-spec.ja.md) | USBTMC / USB488 と SCPI | [`EspUsbHostUsbtmcScpi`](../examples/Vendor/EspUsbHostUsbtmcScpi/) |
| [ccid-api-spec.ja.md](ccid-api-spec.ja.md) | CCIDスマートカードリーダ、ATR、FeliCa | [`Ccid`](../examples/Ccid/) |
| [dp100-spec.ja.md](dp100-spec.ja.md) | ALIENTEK DP100：HIDレポートに載った独自フレーム | [`EspUsbHostDp100Power`](../examples/HID/EspUsbHostDp100Power/) |
| [usb-network-spec.ja.md](usb-network-spec.ja.md) | CDC-NCM / CDC-ECM と lwIP netif接続 | [`UsbNetwork`](../examples/UsbNetwork/) |
| [usb-display-spec.ja.md](usb-display-spec.ja.md)（プロトコル部の英語版: [usb-display-spec.md](usb-display-spec.md)） | DL-1xxのbulkディスプレイプロトコル（AX206とスマートスクリーンのプロトコルは各サンプルのREADMEにあり、[usb-display.ja.md](usb-display.ja.md) が索引） | [`EspUsbHostDisplayDl1xx`](../examples/Vendor/EspUsbHostDisplayDl1xx/) |

## 設計提案

APIを追加する前に書いた設計文書です。判断の背景を残すために保存しています。**日本語のみ。**

| ドキュメント | 状態 |
|-------------|------|
| [lifecycle-listener-proposal.ja.md](lifecycle-listener-proposal.ja.md) | 採用・実装済み |
| [midi-cable-discovery-proposal.ja.md](midi-cable-discovery-proposal.ja.md) | cable数は実装済み、cable名は未着手 |

## 自動生成レポート

CIが生成するもので、手書きではありません。どちらのワークフローもpushごとではなくリリース後に手動起動するため、再実行するまでは現行バージョンに追いついていません。

| ドキュメント | 内容 |
|-------------|------|
| [FOOTPRINT.md](FOOTPRINT.md) | 機能ごとのFlash/RAM使用量。[`tools/footprint_sketches`](../tools/footprint_sketches/) のプローブスケッチで計測。`footprint.json` が正規化済みの元データ |
| `COMPATIBILITY.<version>.md` | リリースごとの、arduino-esp32コアバージョン×ターゲットのビルド結果。リリース後にその版のファイルが1つ追加されるので、使っている版のものをこのディレクトリから選ぶ |

## リポジトリ内の他の場所

| 場所 | 内容 |
|------|------|
| [README.ja.md](../README.ja.md) | APIリファレンス、対応クラス、クラス別の状況、サンプル一覧 |
| [examples/](../examples/) | 実行可能なサンプル。各ディレクトリにREADMEあり |
| [tests/manual/README.ja.md](../tests/manual/README.ja.md) | マニュアルテストのカタログ、既知のハブ問題、チャネル数の制限 |
| [tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md) | テスト戦略と各カテゴリの位置づけ |
| [tests/probe/README.ja.md](../tests/probe/README.ja.md) | ブリングアップとプロトコル解析用の使い捨てスケッチ |
| [CHANGELOG.md](../CHANGELOG.md) | リリース履歴（日英併記） |
