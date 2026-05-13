# テスト

> English: [README.md](README.md)

EspUsbHostの自動テストが含まれるディレクトリです。[pytest-embedded](https://docs.espressif.com/projects/pytest-embedded/en/latest/) のArduino CLIバックエンドを使い、実際のESP32ハードウェア上でテストを実行します。

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

ESP32-S3を2台使用します。1台はEspUsbHostをUSBホストとして実行し、もう1台はArduino USBデバイスのスケッチをピアとして実行します。2台はUSBで接続します。

ハードウェア接続の方法とテストカバレッジの詳細は [peer/README.ja.md](peer/README.ja.md) を参照してください。

### `loopback/` — 1台構成テスト（整備中）

ESP32-P4を1台使い、同一チップ上でUSBホストとUSBデバイスの両方を実行します。2台目のボードは不要です。現在このディレクトリは整備中です。

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
