# ピアテスト

> English: [README.md](README.md)

`tests/peer` には2台構成のテストが含まれています。1台目のESP32-S3がEspUsbHostスケッチをUSBホストとして実行し、もう1台のESP32-S3が対応するUSB Deviceスケッチをピアとして実行します。

多くの peer テストは、Host 側が Arduino Core 標準 Device 実装とも相互運用できることを確認するための基準テストとして維持します。`usb_vendor` は例外で、Arduino Core に非HID vendor-specific bulk/control device API がないため、兄弟ライブラリ `EspUsbDevice` の `EspUsbDeviceVendor` と組み合わせます。

`tests/` ディレクトリから実行：

```sh
uv run --env-file .env pytest peer/
```

## ハードウェア接続

ホストボードとピアボードはUSBで接続する必要があります。

通常のUSBケーブルで接続するとVBUS（5V）ラインも共有されます。両ボードがそれぞれ別のUSB接続（PCなど）からすでに電源を取っている場合、電源ラインの競合が起きることがあります。その場合はデータラインのみを接続するほうが安全です。

ESP32-S3のUSB D−とD+はGPIO19とGPIO20です。VBUSラインは接続せず、この2ピンとGNDだけを2台間でつなぎます：

| ホストボード | ピアボード |
|------------|----------|
| GPIO19 (D−) | GPIO19 (D−) |
| GPIO20 (D+) | GPIO20 (D+) |
| GND | GND |

> **注意:** VBUSラインをカットしたUSBケーブルやデータ専用ケーブルを使う場合は、電源の競合を気にせず通常通り接続できます。

ピアテストで使用するArduino CLIプロファイル名：

- `s3_peer_host`: EspUsbHostを実行するESP32-S3 USBホストボード
- `s3_peer_device`: ESP32-S3 USBデバイスピア

`.env` にシリアルポートを設定：

```sh
TEST_SERIAL_PORT_S3_PEER_HOST=/dev/ttyACM0
TEST_SERIAL_PORT_PEER_DEVICE_S3_PEER_DEVICE=/dev/ttyUSB0
```

現在のカバレッジ：

- `hid_logic`: ピアデバイスを必要としないHIDヘルパーロジックの検証
- `custom_hid`: Arduino Core標準USB Device実装のCustom HID相当とペアで動作
- `hid_keyboard`: Arduino Core標準USB keyboard実装とペアで動作
- `hid_mouse`: Arduino Core標準USB mouse実装とペアで動作
- `hid_keyboard_mouse`: Arduino Core標準keyboard + mouse composite実装とペアで動作
- `hid_consumer_control`: Arduino Core標準consumer control実装とペアで動作
- `hid_system_control`: Arduino Core標準system control実装とペアで動作
- `hid_gamepad`: Arduino Core標準gamepad実装とペアで動作
- `hid_vendor`: Arduino Core標準vendor HID実装とペアで動作
- `usb_serial`: Arduino Core標準USB CDC実装とペアで動作
- `usb_midi`: Arduino Core標準USB MIDI実装とペアで動作
- `usb_audio`: `USBAudioCard` のスピーカー出力を使い、Arduino Core標準USB Audio device相当とペアで動作
- `usb_vendor`: 兄弟ライブラリ `EspUsbDevice` の `EspUsbDeviceVendor` とペアで動作

追加予定のカバレッジ：

- Arduino Core標準Device実装で再現できるHost側回帰テスト
- `EspUsbDevice` 側で見つかったHost側不具合の再現最小ケース
