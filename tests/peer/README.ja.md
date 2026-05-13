# ピアテスト

> English: [README.md](README.md)

`tests/peer` には2台構成のテストが含まれています。1台目のESP32-S3がEspUsbHostスケッチをUSBホストとして実行し、もう1台のESP32-S3がArduino USBデバイスのスケッチをピアとして実行します。

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
- `custom_hid`: `USB/examples/CustomHIDDevice` とペアで動作
- `hid_keyboard`: Arduino USBキーボードサンプルとペアで動作
- `hid_mouse`: `USB/examples/Mouse/ButtonMouseControl` とペアで動作
- `hid_keyboard_mouse`: `USB/examples/KeyboardAndMouseControl` とペアで動作
- `hid_consumer_control`: `USB/examples/ConsumerControl` とペアで動作
- `hid_system_control`: `USB/examples/SystemControl` とペアで動作
- `hid_gamepad`: `USB/examples/Gamepad` とペアで動作
- `hid_vendor`: `USB/examples/HIDVendor` とペアで動作
- `usb_serial`: `USB/examples/USBSerial` とペアで動作
- `usb_midi`: `USB/examples/MIDI` とペアで動作

追加予定のカバレッジ：

- HID出力レポートおよびフィーチャーレポート
