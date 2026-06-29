# テスト

> English: [README.md](README.md)

EspUsbHostの自動テストと手動テストが含まれるディレクトリです。
テスト全体の方針とカバレッジ一覧は [TEST_PLAN.ja.md](TEST_PLAN.ja.md) を参照してください。[pytest-embedded](https://docs.espressif.com/projects/pytest-embedded/en/latest/) のArduino CLIバックエンドを使い、実際のESP32ハードウェア上でテストを実行します。

## 必要なもの

- [uv](https://docs.astral.sh/uv/) — Pythonパッケージ・環境マネージャー
- [Arduino CLI](https://arduino.github.io/arduino-cli/) — pytest-embeddedがスケッチのビルドとフラッシュに内部で使用
- 対象ボードをUSBでホストPCに接続済みであること（書き込みとシリアルモニター用）

## セットアップ

サンプルの環境ファイルをコピーして、シリアルポートを設定します：

```sh
cp .env.example .env
```

`.env.example` の内容：

```sh
# ボードプロファイルごとのシリアルポート
TEST_SERIAL_PORT_S3_PEER_HOST=/dev/ttyACM0
TEST_SERIAL_PORT_PEER_DEVICE_S3_PEER_DEVICE=/dev/ttyUSB0
TEST_SERIAL_PORT_S3_HUB_HOST=/dev/ttyACM1

# オプション: HTMLレポートを生成する場合
#PYTEST_ADDOPTS="--html=report.html --self-contained-html"
```

各 `TEST_SERIAL_PORT_*` 変数を、接続しているボードの実際のシリアルポートに設定してください。

プロファイル名は接続形態を表します。`peer/` は常時接続された専用ボードを使うため、`s3_peer_host`、`s3_peer_device` のような専用プロファイルを使います。`examples/`、`manual/`、`probe/` は個別実行用の汎用ボードを想定し、`esp32s3`、`esp32p4` を使います。`loopback/` は現在 README だけを残しており、このリポジトリ側に実行用プロファイルはありません。

## テストの実行

`tests/` ディレクトリから実行：

```sh
# すべてのテストを実行
uv run --env-file .env pytest

# peerテストのみ実行
uv run --env-file .env pytest peer/

# 特定のテストを実行
uv run --env-file .env pytest peer/hid_logic
uv run --env-file .env pytest peer/hid_keyboard
```

テスト結果は実行間で保存されます（`--save-state` がデフォルトで有効）。前回失敗したテストだけを再実行する場合：

```sh
uv run --env-file .env pytest --lf
```

HTMLレポートを生成する場合は、`.env` の `PYTEST_ADDOPTS` のコメントを外すか、直接オプションを渡します：

```sh
uv run --env-file .env pytest --html=report.html --self-contained-html
```

## テストディレクトリ

### `peer/` — 2台構成テスト

ESP32-S3を2台使用します。1台はEspUsbHostをUSBホストとして実行し、もう1台はArduino-ESP32標準 USB Device 実装のスケッチをピアとして実行します。2台はUSBで接続します。

このディレクトリは Arduino Core 標準 Device 実装との相互運用性確認を主目的にします。兄弟ライブラリ `EspUsbDevice` との詳細な組み合わせテストと ESP32-P4 loopback は、主に `EspUsbDevice` リポジトリ側で扱います。

ハードウェア接続の方法とテストカバレッジの詳細は [peer/README.ja.md](peer/README.ja.md) を参照してください。

### `loopback/` — 1台構成テスト（整備中）

ESP32-P4を1台使い、同一チップ上でUSBホストとUSBデバイスの両方を実行するテスト用の予約ディレクトリです。現在このリポジトリには実行可能な loopback テストはなく、実運用上の主な loopback 整備は `EspUsbDevice` 側で進めています。

### `probe/` — 初期切り分け用プローブ

ESP32-P4のUSBポート識別、HS/FS Host、HS Device、Hardware CDC/JTAGの確認に使うスケッチです。ボード配線やPC側認識に依存するため、正式な回帰テストではありません。詳細は [probe/README.ja.md](probe/README.ja.md) を参照してください。

## pytest-embedded-arduino-cli

[pytest-embedded-arduino-cli](https://github.com/tanakamasayuki/pytest-embedded-arduino-cli) は、pytest-embeddedとArduino CLIを繋ぐプラグインです。テスト実行前にスケッチのビルドとフラッシュを自動的に行います。

### シリアルポートの解決順序

プラグインは各ボードのシリアルポートを以下の順序で解決します：

1. `--port` CLIオプション
2. `TEST_SERIAL_PORT_<PROFILE>` 環境変数（`<PROFILE>` は sketch.yaml のプロファイル名を**大文字にしてハイフンをアンダースコアに変換**したもの）
3. `TEST_SERIAL_PORT` 環境変数（任意のプロファイルのフォールバック）

たとえば、sketch.yaml の `s3_peer_host` というプロファイル名は `.env` の `TEST_SERIAL_PORT_S3_PEER_HOST` に対応します。

常時接続するテスト設備では `TEST_SERIAL_PORT_S3_PEER_HOST`、`TEST_SERIAL_PORT_PEER_DEVICE_S3_PEER_DEVICE` のように専用ポートを設定します。`examples/`、手動テスト、プローブのような個別実行では `TEST_SERIAL_PORT_ESP32S3`、`TEST_SERIAL_PORT_ESP32P4` を使います。

### sketch.yaml とプロファイル

各テストスケッチには、ボードプロファイルを定義する `sketch.yaml` があります：

```yaml
profiles:
  s3_peer_host:
    fqbn: esp32:esp32:esp32s3
default_profile: s3_peer_host
```

ピアボードのスケッチはメインスケッチと同じディレクトリの `peer_<名前>/` サブディレクトリに置き、それぞれ独自の `sketch.yaml` を持ちます。

### 実行モード

```sh
# ビルド・フラッシュ・テストをすべて実行（デフォルト）
uv run --env-file .env pytest peer/hid_keyboard

# ビルドのみ — ボード不要
uv run --env-file .env pytest peer/hid_keyboard --run-mode=build

# テストのみ — ビルドとフラッシュをスキップし、書き込み済みのファームウェアを使用
uv run --env-file .env pytest peer/hid_keyboard --run-mode=test
```

### 詳細出力

```sh
# コンパイルとアップロードコマンドを表示
uv run --env-file .env pytest -v

# 実行コンテキスト全体を表示（スケッチディレクトリ・プロファイル・ポートなど）
uv run --env-file .env pytest -vv
```

### Arduino CLI のセットアップ

Arduino CLI が `PATH` に存在し、必要なボードコアがインストールされている必要があります。パッケージインデックスは定期的に更新してください：

```sh
arduino-cli core update-index
arduino-cli lib update-index
```

## 依存関係

Pythonの依存関係は `pyproject.toml` で宣言され、`uv.lock` でロックされています。`uv run` は初回実行時にローカルの仮想環境へ自動的にインストールします。

主なパッケージ：

| パッケージ | 役割 |
|-----------|------|
| `pytest` | テストランナー |
| `pytest-embedded` | 組み込みデバイステストフレームワーク |
| `pytest-embedded-serial` | ボードとのシリアル通信 |
| `pytest-embedded-arduino-cli` | Arduino CLIによるビルドとフラッシュ |
| `pytest-html` | オプションのHTMLレポート生成 |
