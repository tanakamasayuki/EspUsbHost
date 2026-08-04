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
- `hid_keyboard`: Arduino Core標準USB keyboard実装とペアで動作。HID listenerと単一callbackの共存、上限・無効操作の失敗、解除、登録順、mutable callback状態の継続、callback内変更の次event反映も検証
- `hid_mouse`: Arduino Core標準USB mouse実装とペアで動作
- `hid_keyboard_mouse`: Arduino Core標準keyboard + mouse composite実装とペアで動作
- `hid_keyboard_nkro`: 兄弟ライブラリ `EspUsbDevice` の `EspUsbDeviceHidKeyboard`（NKRO 有効）とペアで動作。ビットマップレポートのデコード（8 キー同時押し）と、report protocol 中でも `setKeyboardLeds()` が届くことを検証
- `hid_keyboard_composite`: `EspUsbDevice` の複合 HID device（keyboard + consumer control + mouse を report ID 付き 1 interface に統合、boot interface なし）とペアで動作。各入力が対応する Host コールバックに届くことと、`setKeyboardLeds()` が report ID 付き LED output report で届くことを検証
- `hid_consumer_control`: Arduino Core標準consumer control実装とペアで動作
- `hid_system_control`: Arduino Core標準system control実装とペアで動作。単一callbackなしのlistener単独配送も検証
- `hid_gamepad`: Arduino Core標準gamepad実装とペアで動作
- `hid_vendor`: Arduino Core標準vendor HID実装とペアで動作
- `usb_serial`: Arduino Core標準USB CDC実装とペアで動作
- `usb_midi`: Arduino Core標準USB MIDI実装とペアで動作。MIDIとdevice lifecycleのlistener APIもここで検証する。1つのテストは意図的にpeerを再起動する。device側のcoreにUSB detach APIがなく、hostに本物の切断を渡す手段が再起動しかないため。
- `usb_audio`: `USBAudioCard` のスピーカー出力を使い、Arduino Core標準USB Audio device相当とペアで動作。UAC1
- `usb_audio_uac2`: 兄弟ライブラリ `EspUsbDevice` の `EspUsbAudioFunction` をUAC2指定にしたheadsetとペアで動作。`USBAudioCard` はUAC1専用のため、Arduino Core標準device stackでは作れない唯一のaudio構成である。class revision、Clock Sourceのサンプルレート（UAC2 descriptorが持たないため `SAM_FREQ` の `RANGE` リクエストで取得）、4バイト・2ビットのFeature Unit controlとvolumeの `RANGE` リクエスト、feedback IN endpointがstream一覧に出ずポーリングされてOUTパケットのレート追従に使われること（`f`: 報告レートが48 kHz近傍、追従レートが一致、更新が継続すること）、OUT/IN streaming、およびUAC2固有ではないが引数を0にした「最良フォーマット自動選択」での起動を検証する
- `usb_vendor`: 兄弟ライブラリ `EspUsbDevice` の `EspUsbDeviceVendor` とペアで動作
- `usb_ncm`: 兄弟ライブラリ `EspUsbDevice` の `EspUsbDeviceNet`（CDC-NCM）device とペアで動作。Host は USB NIC を DHCP クライアントの lwIP netif として attach し、device の DHCP サーバから `192.168.7.x` のリースを取得して、固定ページを HTTP GET で取得する。

追加予定のカバレッジ：

- Arduino Core標準Device実装で再現できるHost側回帰テスト
- `EspUsbDevice` 側で見つかったHost側不具合の再現最小ケース
