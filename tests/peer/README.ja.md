# ピアテスト

> English: [README.md](README.md)

`tests/peer` には2台構成のテストが含まれています。1台目のESP32-S3がEspUsbHostスケッチをUSBホストとして実行し、もう1台のESP32-S3が対応するUSB Deviceスケッチをピアとして実行します。

多くの peer テストは、Host 側が Arduino Core 標準 Device 実装とも相互運用できることを確認するための基準テストとして維持します。兄弟ライブラリ `EspUsbDevice` とペアにするのは、Arduino Core ではそのデバイスを表現できない場合だけです（`usb_vendor`、`usb_ncm`、`usb_ncm_throughput`、`hid_keyboard_composite`、`hid_keyboard_nkro`、`usb_audio_uac2`）。詳細は [../TEST_PLAN.ja.md](../TEST_PLAN.ja.md) を参照してください。

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
- `hid_mouse_report`: issue #39で報告されたLogitech G502 HEROのreport protocolレイアウト（16ボタン・16bit X/Y・wheel・AC Panの8バイト、report IDなし）を宣言するcustom HIDデバイスとペアで動作。boot layout決め打ちではなくreport descriptorからフィールド位置を特定していることを検証する（16bitの移動量、縦移動だけのレポートが無変化に見えずイベントになること、X移動がwheelに漏れないこと、AC Pan、`buttonMask` の上位バイトに入るボタン16）。`hid_mouse` が使うArduino Coreの `USBHIDMouse` はboot layoutしか作れず、それはdescriptorを読まなくても動く唯一のレイアウトである。
- `hid_keyboard_mouse`: Arduino Core標準keyboard + mouse composite実装とペアで動作
- `hid_keyboard_nkro`: 兄弟ライブラリ `EspUsbDevice` の `EspUsbDeviceHidKeyboard`（NKRO 有効）とペアで動作。ビットマップレポートのデコード（8 キー同時押し）と、report protocol 中でも `setKeyboardLeds()` が届くことを検証
- `hid_keyboard_composite`: `EspUsbDevice` の複合 HID device（keyboard + consumer control + mouse を report ID 付き 1 interface に統合、boot interface なし）とペアで動作。各入力が対応する Host コールバックに届くことと、`setKeyboardLeds()` が report ID 付き LED output report で届くことを検証
- `hid_consumer_control`: Arduino Core標準consumer control実装とペアで動作
- `hid_system_control`: Arduino Core標準system control実装とペアで動作。単一callbackなしのlistener単独配送も検証
- `hid_gamepad`: Arduino Core標準gamepad実装とペアで動作
- `hid_vendor`: Arduino Core標準vendor HID実装とペアで動作
- `usb_serial`: Arduino Core標準USB CDC実装とペアで動作
- `usb_midi`: Arduino Core標準USB MIDI実装とペアで動作。MIDIとdevice lifecycleのlistener APIもここで検証する。1つのテストは意図的にpeerを再起動する。device側のcoreにUSB detach APIがなく、hostに本物の切断を渡す手段が再起動しかないため。
- `usb_msc`: Arduino Core標準の `USBMSC` device（16ブロック×512バイトのRAMディスク）とペアで動作。容量取得（32bit・64bit両方）、Inquiry、Max LUNとLUN選択、Request Sense、Test Unit Ready / 準備待ち、Synchronize Cache、peer側メモリと突き合わせる単一・複数ブロックおよび分割転送のwrite/read往復、範囲外アクセスの拒否、write失敗の報告（peerが要求に応じて1回のwriteを失敗させる）を検証。さらに`end()`/再開を2通り検証する。デバイス接続中の場合と、peer再起動でdevice listが空の場合で、後者はhost libraryがinstallされたまま残っていたケース（issue #42）
- `usb_msc_fat`: Arduino Core標準の `USBMSC` device とペアで動作。256ブロック×512バイトのRAMディスクを、peer自身がUSB開始前に`f_mkfs()`で整形し`PEER.TXT`を書き込む。hostがマウントするのと同じFatFsで作ったボリュームになる。`mscMount()` / `mscUnmount()` / `mscMounted()`、VFSパス経由でのpeerのファイル読み出し、報告された手順（mount→unmount→`end()`→`begin()`→再mount）、およびマウント中に`end()`してもFatFsのドライブスロットとVFSパスが取り残されないこと（issue #42）を検証する。なおこのpeerはSYNCHRONIZE CACHEの応答が安定せず、同じコマンドが1回目は成功し次はCSW status 1で返ってくる。そのため`y`コマンドは診断用で、検証対象にはしていない。ライブラリはデバイス単位でこのコマンドを諦める動作に落ち、どちらでもunmountは成功する
- `usb_audio`: `USBAudioCard` のスピーカー出力を使い、Arduino Core標準USB Audio device相当とペアで動作。UAC1
- `usb_audio_uac2`: 兄弟ライブラリ `EspUsbDevice` の `EspUsbAudioFunction` をUAC2指定にしたheadsetとペアで動作。`USBAudioCard` はUAC1専用のため、Arduino Core標準device stackでは作れない唯一のaudio構成である。class revision、Clock Sourceのサンプルレート（UAC2 descriptorが持たないため `SAM_FREQ` の `RANGE` リクエストで取得）、4バイト・2ビットのFeature Unit controlとvolumeの `RANGE` リクエスト、feedback IN endpointがstream一覧に出ずポーリングされてOUTパケットのレート追従に使われること（`f`: 報告レートが48 kHz近傍、追従レートが一致、更新が継続すること）、OUT/IN streaming、およびUAC2固有ではないが引数を0にした「最良フォーマット自動選択」での起動を検証する
- `usb_vendor`: 兄弟ライブラリ `EspUsbDevice` の `EspUsbDeviceVendor` とペアで動作
- `usb_ncm`: 兄弟ライブラリ `EspUsbDevice` の `EspUsbDeviceNet`（CDC-NCM）device とペアで動作。Host は USB NIC を DHCP クライアントの lwIP netif として attach し、device の DHCP サーバから `192.168.7.x` のリースを取得して、固定ページを HTTP GET で取得する。
- `usb_ncm_throughput`: 同じ組み合わせに連続負荷をかける。device 側の TCP sink（port 9000）と TCP source（port 9001）へ各5秒間フルレートで流し、両方向が動き続けること、bulk OUT が失敗を報告しないこと、交渉したサイズを超えて破棄される NTB が無いことを確認する。peer の `build_opt.h` で `CFG_TUD_NCM_IN_NTB_MAX_SIZE` を 8192・送信バッファ3面に上げ、device 側はバースト書き込みするため、実売 USB NIC と同様に複数 datagram を1つの NTB にまとめる。これが、以前は固定3200バイトだった host 側バッファを超える NTB を丸ごと破棄していた問題を露出させた構成である。この peer 設定は維持すること: `usb_ncm` は 1NTB=1datagram しか作らず、この回帰を検出できない。これらはコンパイラフラグなので、`build_opt.h` を触った後は `--clean` を付けて実行する。付けないとキャッシュされた `EspUsbDevice` のビルドが再利用され、peer は以前のサイズを申告する（テストは交渉値を検査しているので、その場合は明確に失敗する）。

追加予定のカバレッジ：

- Arduino Core標準Device実装で再現できるHost側回帰テスト
- `EspUsbDevice` 側で見つかったHost側不具合の再現最小ケース
