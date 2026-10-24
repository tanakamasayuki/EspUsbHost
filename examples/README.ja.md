# EspUsbHost サンプル案内

[English](README.md)

まず[サンプルの進め方](../docs/example-workflow.ja.md)で導入・接続・callback・戻り値を確認します。以下は各スケッチの概要です。

1. [BringUpCheck](Info/EspUsbHostBringUpCheck/README.ja.md)でHost開始と相手の列挙を分けて確認します。
2. [DeviceInfo](Info/EspUsbHostDeviceInfo/README.ja.md)でVID/PID・interface・endpointを読みます。
3. [Keyboard](HID/EspUsbHostKeyboard/README.ja.md)等の手持ちの機器に合う例を一つ動かします。
4. 未知のHIDは[RawDump](HID/EspUsbHostHIDRawDump/README.ja.md)と[ReportDescriptor](Info/EspUsbHostHIDReportDescriptor/README.ja.md)で調べます。
5. 複数機器・hub・音声・独自protocolは、単体の認識と通信を確認してから加えます。

| 例 | 学ぶこと |
| --- | --- |
| [EspUsbHostAudioInput](Audio/EspUsbHostAudioInput/README.ja.md) | USBマイクのPCMを受け、信号のピークを調べる。 |
| [EspUsbHostAudioOutputHardwareVolume](Audio/EspUsbHostAudioOutputHardwareVolume/README.ja.md) | 生成PCMの振幅とUSB機器側の音量制御を区別する。 |
| [EspUsbHostAudioOutputMP3ESP8266Audio](Audio/EspUsbHostAudioOutputMP3ESP8266Audio/README.ja.md) | 埋め込みMP3をデコードし、USB音声出力へ渡す。 |
| [EspUsbHostAudioOutputMP3PCMFlow](Audio/EspUsbHostAudioOutputMP3PCMFlow/README.ja.md) | PCMFlowのデコード結果を選択したUSB音声streamへ接続する。 |
| [EspUsbHostAudioOutputTone](Audio/EspUsbHostAudioOutputTone/README.ja.md) | 対応するUSB音声出力へPCMの試験音を生成する。 |
| [EspUsbHostCcidFelicaIdm](Ccid/EspUsbHostCcidFelicaIdm/README.ja.md) | 選んだSystem CodeでFeliCaのIDmを読む。 |
| [EspUsbHostCcidReader](Ccid/EspUsbHostCcidReader/README.ja.md) | カードを活性化し、ATRとUID要求への応答を調べる。 |
| [EspUsbHostCompositeHID](HID/EspUsbHostCompositeHID/README.ja.md) | 複合HID機器のキーボード・マウス・メディア入力を読む。 |
| [EspUsbHostConsumerControl](HID/EspUsbHostConsumerControl/README.ja.md) | メディアキーの押下・解放を受け取る。 |
| [EspUsbHostDp100Power](HID/EspUsbHostDp100Power/README.ja.md) | DP100電源をHIDで読み、必要な場合だけ設定を変更する。 |
| [EspUsbHostGamepad](HID/EspUsbHostGamepad/README.ja.md) | ゲームパッドの各fieldをHID usageから調べる。 |
| [EspUsbHostHIDRawDump](HID/EspUsbHostHIDRawDump/README.ja.md) | アプリ固有の解釈を加える前の生HID入力を表示する。 |
| [EspUsbHostHIDVendor](HID/EspUsbHostHIDVendor/README.ja.md) | Vendor HID入力を受け、対応するOutput・Feature reportを送る。 |
| [EspUsbHostKeyboard](HID/EspUsbHostKeyboard/README.ja.md) | USBキーボードの文字入力とLock LEDの状態を読む。 |
| [EspUsbHostKeyboardDump](HID/EspUsbHostKeyboardDump/README.ja.md) | キーボードのusage・修飾キー・変換後の文字を調べる。 |
| [EspUsbHostKeyboardNKRO](HID/EspUsbHostKeyboardNKRO/README.ja.md) | 保持キーのbitmapと複数キーの変化を観察する。 |
| [EspUsbHostMacroPadN3](HID/EspUsbHostMacroPadN3/README.ja.md) | LCDキー画像を描き、マクロパッドの操作を受け取る。 |
| [EspUsbHostMouse](HID/EspUsbHostMouse/README.ja.md) | マウスの移動と各ボタンの押下・解放を読み解く。 |
| [EspUsbHostSystemControl](HID/EspUsbHostSystemControl/README.ja.md) | 電源操作等のSystem Control HID入力を受け取る。 |
| [EspUsbHostBringUpCheck](Info/EspUsbHostBringUpCheck/README.ja.md) | Hostの開始と接続機器の列挙を分けて確認する。 |
| [EspUsbHostCustomDeviceCallbacks](Info/EspUsbHostCustomDeviceCallbacks/README.ja.md) | interface情報を使って接続機器を分類する。 |
| [EspUsbHostDeviceExplorer](Info/EspUsbHostDeviceExplorer/README.ja.md) | 未知の機器を調べ、適切な通信方法を選ぶ。 |
| [EspUsbHostDeviceInfo](Info/EspUsbHostDeviceInfo/README.ja.md) | 接続機器とinterface・endpointの情報を表示する。 |
| [EspUsbHostHIDReportDescriptor](Info/EspUsbHostHIDReportDescriptor/README.ja.md) | HID入力byteの解釈に使うreport descriptorを取得する。 |
| [EspUsbHostHubPPPS](Info/EspUsbHostHubPPPS/README.ja.md) | hub各portの給電状態を調べ、個別の切り替えを要求する。 |
| [EspUsbHostP4FsPhyRouting](Info/EspUsbHostP4FsPhyRouting/README.ja.md) | P4のFullSpeed PHY経路を選んでからHostを開始する。 |
| [EspUsbHostMIDI](MIDI/EspUsbHostMIDI/README.ja.md) | USB MIDI入力を解釈し、明示的な操作で試験メッセージを送る。 |
| [EspUsbHostDisplayTuring](Serial/EspUsbHostDisplayTuring/README.ja.md) | 対応するTuring USBシリアル画面へ描画する。 |
| [EspUsbHostMultiUSBSerial](Serial/EspUsbHostMultiUSBSerial/README.ja.md) | 複数のシリアルwrapperを相手のUSB addressへ結び付ける。 |
| [EspUsbHostUSBSerial](Serial/EspUsbHostUSBSerial/README.ja.md) | USBシリアル機器と基板のログ用シリアルを中継する。 |
| [EspUsbHostMSCBlockDump](Storage/EspUsbHostMSCBlockDump/README.ja.md) | ストレージ容量を読み、先頭の生blockを調べる。 |
| [EspUsbHostMSCFatList](Storage/EspUsbHostMSCFatList/README.ja.md) | FATストレージをmountし、一覧と書き込み・読み戻し・削除を試す。 |
| [UsbNetwork](UsbNetwork/README.ja.md) | USBネットワークアダプタを接続し、IP通信を行う。 |
| [EspUsbHostAdbConnect](Vendor/EspUsbHostAdbConnect/README.ja.md) | ADBを認証し、範囲を限定したshellのecho試験を行う。 |
| [EspUsbHostDisplayAx206](Vendor/EspUsbHostDisplayAx206/README.ja.md) | 対応するAX206 USB画面へ描画する。 |
| [EspUsbHostDisplayDl1xx](Vendor/EspUsbHostDisplayDl1xx/README.ja.md) | 対応するDL-1xx USB映像アダプタへ描画する。 |
| [EspUsbHostPrinterEscPos](Vendor/EspUsbHostPrinterEscPos/README.ja.md) | プリンタの状態を調べ、必要な場合だけESC/POSの試験印刷を行う。 |
| [EspUsbHostProtocolConsole](Vendor/EspUsbHostProtocolConsole/README.ja.md) | 診断consoleからcontrol・bulk要求を明示的に送る。 |
| [EspUsbHostUsbtmcScpi](Vendor/EspUsbHostUsbtmcScpi/README.ja.md) | 対応USBTMC計測器とSCPI命令をやり取りする。 |
| [EspUsbHostVendorBulk](Vendor/EspUsbHostVendorBulk/README.ja.md) | 対応interfaceを開き、Vendor bulkとcontrol転送を試す。 |
| [EspUsbHostVideoCamera](Video/EspUsbHostVideoCamera/README.ja.md) | UVC streamを選び、完成frameをメイン処理へ渡す。 |
