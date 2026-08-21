# EspUsbHost

> English: [README.md](README.md)

ESP32-S3 / ESP32-S2 / ESP32-P4でUSB Hostを使うためのArduinoライブラリです。

USB処理はバックグラウンドのFreeRTOSタスクで行われるため、`loop()`でUSBポーリング関数を呼ぶ必要はありません。`setup()`でコールバックを登録して`begin()`を呼ぶだけで動作します。

USB Hostが初めての方、あるいは手元のデバイスが動かない方は、まず **[USB Host開発ガイド](docs/usb-host-guide.ja.md)** をご覧ください。USBの基礎、ESP32シリーズ固有の制限（電源・速度・ハブ・チャネル数）、ボードの確認から未知デバイスの識別、仕様非公開プロトコルの解析までの手順をまとめています。さらに踏み込んだ内容（ディスクリプタのバイト構造、ホストチャネルとFIFO分割、エラー復帰、スループット設計、コールバックのコンテキスト、独自クラスの実装）は **[上級編](docs/usb-host-advanced.ja.md)** にあります。実機で確認済みのデバイスとボードは [動作確認済みデバイスとボード](docs/tested-devices.ja.md) に、`docs/` 以下の全文書は [docs/README.ja.md](docs/README.ja.md) にまとめています。

## 目次

- [対応環境](#対応環境)
- [バージョン2系の位置づけ](#バージョン2系の位置づけ)
- [兄弟ライブラリ: EspUsbDevice](#兄弟ライブラリ-espusbdevice)
- [機能](#機能)
- [対応USBクラス一覧](#対応usbクラス一覧)
- [ロードマップ](#ロードマップ)
- [現状の制限と注意点](#現状の制限と注意点)
- [ハードウェア要件](#ハードウェア要件)
- [インストール](#インストール)
- [クイックスタート](#クイックスタート)
- [サンプル一覧](#サンプル一覧)
- [APIリファレンス](#apiリファレンス)
  - [コア](#コア)
  - [デバイスイベント](#デバイスイベント)
  - [HID入力](#hid入力)
  - [HID出力](#hid出力)
  - [USBシリアル（CDC ACM・VCP）](#usbシリアルcdc-acmvcp)
  - [Vendor bulk/control](#vendor-bulkcontrol)
  - [CCIDスマートカードリーダー](#ccidスマートカードリーダー)
  - [MIDI](#midi-1)
  - [USBオーディオ](#usbオーディオ)
  - [USB Mass Storage](#usb-mass-storage)
  - [USB Hub](#usb-hub)
  - [USBネットワーク（CDC-NCM / CDC-ECM）](#usbネットワークcdc-ncm--cdc-ecm)
  - [デバイス探索](#デバイス探索)
  - [エラーハンドリング](#エラーハンドリング)
- [設計方針](#設計方針)
- [複数デバイスの扱い](#複数デバイスの扱い)
- [テスト](#テスト)
- [リリースチェックリスト](#リリースチェックリスト)
- [ライセンス](#ライセンス)

## 対応環境

対応する Arduino-ESP32 コア（ボードパッケージ）の最低バージョン:

| ターゲット | 最低 arduino-esp32 |
| --- | --- |
| ESP32-S2 / ESP32-S3 | 3.2.0 |
| ESP32-P4 | 3.3.1 |

これより古いコアは非対応です（3.1.x 以前はビルドに失敗することがあります）。ライブラリ各バージョンのコア別ビルド結果は [`docs/`](docs/) に `COMPATIBILITY.<version>.md` として公開しています。

主対象は ESP32-S3 で、peer/manual テストもこのターゲットで実行しています。ESP32-S2 は
リリースごとに全対応コアでビルド確認していますが、内蔵RAMが大幅に少ないため
`ESP_USB_HOST_MAX_DEVICES` の既定値が 8 ではなく 3 で（`-DESP_USB_HOST_MAX_DEVICES=N`
で増やせますが `dram0_0_seg overflowed` に注意）、`UsbNetwork` の例は収まりません。
ESP32-P4 は HS OTG が使えます（[ESP32-P4 の注意事項](#esp32-p4-の注意事項)を参照）。

## バージョン2系の位置づけ

バージョン2系は全面的に作り直した実装で、**1系のAPIとは互換性がありません**。従来の継承・仮想関数オーバーライドを中心にした使い方は主要APIではなく、`onKeyboard()`、`onMouse()`、`onDeviceConnected()`、各USBクラス向けのsend/start APIなど、コールバック登録ベースのAPIへ移行してください。

1系より対応USBクラスとサンプルは大幅に増えていますが、すべてのESP-IDF USB class driverを置き換える完全検証済みライブラリではありません。実機で使いやすいArduino USB Host APIを優先しているため、2系の間でもAPIをより単純・安全・一貫した形にするための破壊的変更が入る可能性があります。

テストは1系より増えており、examplesのビルド確認、peer/manualテスト、主要パスの実機確認を進めています。それでもUSB Audio、USB Hubの細かい挙動、複数デバイス構成、非準拠または癖の強いUSBデバイスは、利用する実機ごとの確認が必要です。

## 兄弟ライブラリ: EspUsbDevice

USBデバイス側には、兄弟ライブラリ
[`EspUsbDevice`](https://github.com/tanakamasayuki/EspUsbDevice) があります。
`EspUsbDevice` はこの `EspUsbHost` に対応して拡張している USB Device ライブラリで、
Host / Device の組み合わせテストや ESP32-P4 1台構成の loopback 検証に使います。

Arduino-ESP32 標準の `USB`、`USBHIDKeyboard`、`USBHIDMouse`、`USBCDC` などは、
一般的な USB Device sketch を短く書くには便利です。一方で `EspUsbDevice` は、
port、speed、descriptor、endpoint packet size、HID report ID、output / feature report、
raw class report をスケッチ側から明示的に制御する用途を重視します。

キーボードについても、標準 `USBHIDKeyboard` は文字入力を簡単に送る用途には便利ですが、
日本語配列のすべてのキーや `無変換`、`変換`、`かな`、`半角/全角`、JIS 固有の記号キーなどを
HID usage として正確に扱う用途には限界があります。`EspUsbDevice` では、文字入力 helper だけでなく
raw HID usage / report を直接扱えるようにし、`EspUsbHost` 側のキーボードレイアウト処理も検証します。

通常のキーボード、マウス、CDC などを PC へつなぐだけなら標準ライブラリが第一候補です。
`EspUsbHost` の挙動を詳しく検証したい場合、Arduino Core 標準 Device 実装では制御しづらい
descriptor や report を使いたい場合、または ESP32-P4 で Host / Device loopback を行いたい場合は
`EspUsbDevice` を使います。

## 機能

- **HID入力** — キーボード・マウス・コンシューマーコントロール（メディアキー）・システムコントロール（電源/スタンバイ）・ゲームパッド
- **HID出力** — キーボードLED制御・ベンダー出力/フィーチャーレポート
- **USBシリアル** — CDC ACMおよび主要VCPデバイス（FTDI・CP210x・CH34x）を`EspUsbHostCdcSerial`で統一対応（Arduino `Stream`/`Print` 互換）
- **MIDI** — USB MIDI入出力
- **USBオーディオ** — USB Audio StreamingインターフェースのIsochronous INペイロード受信とIsochronous OUT送信
- **USB Mass Storage** — USB Mass Storage Bulk-Only TransportのSCSI容量取得・ブロックread/write、FatFs/VFSマウント、Arduino `fs::FS` / `File`互換
- **USBネットワーク** — CDC-NCM / CDC-ECMのUSB Ethernetアダプタに対応。生Ethernetフレームでも、lwIP（`esp_netif`）インターフェースとしてattachしてWi-Fi無しで`NetworkClient` / `HTTPClient`をUSB経由で動かすことも可能
- **CCIDスマートカードリーダー** — CCID interfaceのclaim、カード挿入・排出通知、カードの活性化とATR取得、ATRからのカード種別判定（ISO 14443 A/B・ISO 15693・FeliCaなど）、APDU送受信、リーダー固有のescapeコマンド
- **Vendor bulk/control** — HIDではないvendor-specific interfaceのbulk IN/OUT、ゼロコピーバッファと自動ZLP処理を備えた非同期bulk OUTキュー、EP0 vendor request。専用APIを持たないクラスへの入口でもあり、USBTMC計測器やESC/POSプリンタはすべてこの上のexampleとして実装している
- **デバイス探索** — 接続デバイス・インターフェース・エンドポイントの列挙
- **複数デバイス対応** — 各コールバックと送信APIにオプションの`address`引数があり、特定デバイスを指定可能

## 対応USBクラス一覧

このライブラリで扱ったUSBクラスコードと、その扱い方です。**ライブラリAPI**はそのクラス専用のAPIが本体にあるという意味、**example**はライブラリ側は汎用APIのみでクラス固有処理がすべてスケッチ側にあるという意味です。成熟度で見た同じ範囲は次節にあります。

| クラス | コード | 対応の形 | 場所 |
|---|---|---|---|
| Audio（UAC1 / UAC2） | `0x01` | ライブラリAPI — Isochronous INペイロード受信とOUT送信 | [`examples/Audio/`](examples/Audio/) |
| MIDI（Audio subclass 3） | `0x01`/`0x03` | ライブラリAPI — MIDI入出力 | [`examples/MIDI/`](examples/MIDI/) |
| CDC Control / Data（ACM） | `0x02`/`0x0a` | ライブラリAPI — `EspUsbHostCdcSerial`、Arduino `Stream`/`Print` 互換 | [`examples/Serial/`](examples/Serial/) |
| HID | `0x03` | ライブラリAPI — キーボード・マウス・ゲームパッド・コンシューマー/システムコントロール・ベンダーレポート | [`examples/HID/`](examples/HID/) |
| **Printer** | **`0x07`** | **example — ESC/POSレシートプリンタをvendor bulk/control API上で** | [`examples/Vendor/EspUsbHostPrinterEscPos/`](examples/Vendor/EspUsbHostPrinterEscPos/) |
| Mass Storage（BOT/SCSI） | `0x08` | ライブラリAPI — ブロックI/OとFatFs / Arduino `fs::FS` | [`examples/Storage/`](examples/Storage/) |
| Hub | `0x09` | ライブラリAPI — 検出・トポロジ・ポート単位の電源制御（PPPS） | [`examples/Info/`](examples/Info/) |
| Smart Card（CCID） | `0x0b` | ライブラリAPI — ATR、カード種別、APDU送受信、escapeコマンド | [`examples/Ccid/`](examples/Ccid/) |
| Video（UVC） | `0x0e` | **未対応** — 後述のディスクリプタサイズ制限のため | — |
| CDC-NCM / CDC-ECM Ethernet | `0x02` のsubclass | ライブラリAPI — 生フレーム、またはlwIP `esp_netif` | [`examples/UsbNetwork/`](examples/UsbNetwork/) |
| Application Specific（USBTMC） | `0xfe` | example — USBTMC/USB488 + SCPIをvendor bulk/control API上で | [`examples/Vendor/EspUsbHostUsbtmcScpi/`](examples/Vendor/EspUsbHostUsbtmcScpi/) |
| Vendor-specific | `0xff` | ライブラリAPI — interfaceの明示claim、bulk IN/OUT、EP0 request | [`examples/Vendor/`](examples/Vendor/) |

interface classが役に立たないデバイスも同じ道筋で扱います。USBシリアル変換（FTDI・CP210x・CH34x）はvendor-specific interfaceを`EspUsbHostCdcSerial`で駆動し、ALIENTEK DP100電源は素のHID interfaceに独自フレームを載せたデバイスです（[`examples/HID/EspUsbHostDp100Power`](examples/HID/EspUsbHostDp100Power/)）。

`printDeviceInfo()` と [`device_dump`](tests/manual/device_dump/) 実機テストは、対応・非対応にかかわらず挿したデバイスのクラスを表示します。コードを書く前に正体を確認できます。

## ロードマップ

### USBクラス対応状況

| クラス | 状況 |
|--------|------|
| HID — キーボード・マウス・ゲームパッド・コンシューマーコントロール・システムコントロール・ベンダー | ✅ 実装済み |
| USBシリアル — CDC ACM・VCP（FTDI・CP210x・CH34x）を`EspUsbHostCdcSerial`で統一対応。baud、データビット、パリティ、ストップビットを設定可能 | ✅ 実装済み |
| USB MIDI | ✅ 実装済み |
| Vendor-specific bulk/control | ✅ 基本実装済み。明示的なinterface claim、bulk IN/OUT（同期と非同期キュー）、自動ZLP、EP0 vendor IN/OUT requestに対応 |
| CCID — スマートカードリーダー（bulkプロトコル） | ✅ 基本実装済み。interfaceの明示claim、class descriptorのparse、slot状態、power on/offとATR、ATRからのカード種別判定、APDU/XfrBlock送受信、escapeと生メッセージ、slot変化通知に対応。Sony RC-S300で確認済み。ICCD変種、チェイン応答（extended APDU）、PINパッド機能は対象外 |
| USBグラフィックスアダプタ（DL-1xx bulkプロトコル） | 📄 example限りのbest effort。[`examples/Vendor/EspUsbHostDisplayDl1xx`](examples/Vendor/EspUsbHostDisplayDl1xx/) にvendor bulk API上で実装。ライブラリ本体にディスプレイ固有の処理は入っていない。1チップファミリ・16 bppの参考実装であり、他のアダプタや高いフレームレートが必要なら [Pico_USB_Disp](https://github.com/htlabnet/Pico_USB_Disp) のような専用ライブラリを使うこと |
| AX206 USBフォトフレームディスプレイ | 📄 example限りのbest effort。[`examples/Vendor/EspUsbHostDisplayAx206`](examples/Vendor/EspUsbHostDisplayAx206/) に実装。ESP32-S3で2 fpsを実機確認済み。全画面blitしか受け付けないデバイスなので、1フレームが307,200バイトを運ぶBulk-Only Transportトランザクション1回になります |
| USBスマートスクリーン（CDCシリアルプロトコル） | 📄 example限りのbest effort。[`examples/Serial/EspUsbHostDisplayTuring`](examples/Serial/EspUsbHostDisplayTuring/) にCDCシリアル書き込みキュー上で実装。ライブラリ本体にディスプレイ固有の処理は入っていない。3.5インチの `USB35INCHIPSV2`（`1a86:5722`）を16 bppで扱う。他のディスプレイexampleとあわせて [docs/usb-display.ja.md](docs/usb-display.ja.md) に一覧がある |
| USBTMC — SCPI計測器 | 📄 example限りのbest effort。[`examples/Vendor/EspUsbHostUsbtmcScpi`](examples/Vendor/EspUsbHostUsbtmcScpi/) にvendor bulk/control API上で実装。ライブラリ本体にUSBTMC固有の処理は入っていない。interface classは0xFE（Application Specific）でvendor-specificではないが、使うAPIで分類しているためexampleは `Vendor/` にある。菊水電子工業の直流電源 PMX18-5A（`0b3e:1029`）で実機確認済み（class request、bulkメッセージ層、SCPIクエリ）。USB488のinterrupt IN（service request）は使わない |
| Printer — ESC/POSレシートプリンタ | 📄 example限りのbest effort。[`examples/Vendor/EspUsbHostPrinterEscPos`](examples/Vendor/EspUsbHostPrinterEscPos/) にvendor bulk/control API上で実装。ライブラリ本体にプリンタ固有の処理は入っていない。interface classは0x07だが、使うAPIで分類しているためexampleは `Vendor/` にある。Xprinter XP-C58K（`0483:070b`）で実機確認済み（class request 3種、ESC/POSリアルタイムステータス、日本語レシート＋バーコード＋QR＋オートカット）。この機種はclass requestが「応答はするが中身が空」だったため、頼れるステータス経路はリアルタイムステータス（`DLE EOT n`）。IPP / PWG-Raster / PCL と IEEE 1284.4 パケットモードは対象外 |
| ALIENTEK DP100 数控電源 | 📄 example限りのbest effort。[`examples/HID/EspUsbHostDp100Power`](examples/HID/EspUsbHostDp100Power/) にHID API上で実装。ライブラリ本体にDP100固有の処理は入っていない。素のHID interfaceに独自フレームを載せたデバイスなので `onHIDInput()` と `sendHIDVendorOutput()` で足りる。読み取りはESP32-S3で実機確認済み（機器情報、入力電圧、出力V/A、温度。単位も実測で確定）。設定書き込みフレームは出力ON/OFFを含むため実装のみで未検証 |
| Mirabox N3 / Ajazz AKP03系 LCDマクロパッド | 📄 example限りのbest effort。[`examples/HID/EspUsbHostMacroPadN3`](examples/HID/EspUsbHostMacroPadN3/) にHID API上で実装。ライブラリ本体にパッド固有の処理は入っていない。vendor interfaceに独自の`CRT`プロトコルを載せたcomposite HIDデバイスなので `onHIDVendorInput()` と `sendHIDVendorOutput()` で足りる。STREONOR S6（`1500:3006`）で実機確認済み（輝度、クリア、リフレッシュ、6キー画面への64x64 JPEG表示）。パッドのinterrupt OUTがMPS 1024のため、ESP32-P4のHigh-speedポート限定。シーンキーとノブの入力レポートコードは未確定 |
| UAC — USBオーディオ入出力 | 🔲 実験的。UAC1は標準Arduino `USBAudioCard`、UAC2は `EspUsbDevice` peerでAudio OUT/INのpeer確認済み（descriptor、Clock Sourceのサンプルレート、Feature Unitのmute/volume、OUT/IN streaming）。実USBマイク・オーディオIF確認は継続 |
| HUB — ハブ検出・トポロジー情報・ポート電源制御 | ✅ 基本実装済み。`hub_info`と`hub_power`のmanual確認済み。change bit処理、複数段Hub、USB 3.x Hub互換性は継続確認 |
| CDC-NCM / CDC-ECM — 生フレームアクセスとlwIP netif attachによるUSB Ethernet | 🔲 実験的。EspUsbDeviceの`UsbNetwork`sketchおよびAX88179Aアダプタでpeer確認済み。network機能が既定configurationに無いアダプタは`setConfigurationSelector()`と2パスの列挙が必要 |
| MSC — USBストレージのブロックI/OとFatFs/Arduino FSマウント | 🔲 実験的。単一MSCデバイスの基本read/writeとFatFsマウントはpeer/manual確認済み。非準拠デバイス、複数MSC・複数LUN、異常系BOT完全復旧は継続確認 |
| UVC — USBカメラ | ❌ 現在非対応。Arduino-ESP32のプリコンパイル済みUSB HostスタックにあるConfiguration Descriptor長の制限により、一般的なUVCデバイスを列挙できないため |

### その他の予定機能

| 機能 | 状況 |
|------|------|
| `onHIDReportDescriptor()` — HIDレポートディスクリプタの取得 | ✅ 実装済み |
| HIDゲームパッド入力 — ユーザー定義マッピング用のディスクリプタデコード済みフィールドとraw/reportバイト | ✅ マッピング前提のイベントAPI。マッピング補助は検討中 |
| チャンネル数・endpoint使用量の可視化 | ✅ 実験的な診断APIとして実装済み。複数デバイスやAudio/MSC/HUB併用時の上限把握に使う |
| USB Audio INの実データ確認 | ✅ 標準Arduino `USBAudioCard`（UAC1）と `EspUsbDevice` peer（UAC2）でpeer確認済み。実USBマイク・オーディオIF確認は継続 |
| ESP32-P4検証 | 🔲 継続。FS/HS OTG、HUB可否、ループバックテストを個別確認する |
| ループバックテスト（ESP32-P4 1台構成） | 🔲 `EspUsbDevice` 側で整備中。このリポジトリ側の `tests/loopback` はREADMEのみ |
| 手動テスト — VCPシリアル・複数デバイス・活線挿抜 | ✅ 主要ケース確認済み。追加デバイス互換性は継続 |

## 現状の制限と注意点

- **バージョン互換性:** 2系は1系とソース互換ではありません。1系のスケッチは、コールバック登録APIと新しいクラス別APIへ移行してください。
- **今後の破壊的変更:** 実機やサンプルからよりよいAPI形状が見えた場合、2系の間でも互換性を壊す変更が入る可能性があります。
- **USB Hostリソース:** ESP32-S3のUSB host channel数は少ないです。複合デバイス、Hub、Audio、MSC、複数シリアルデバイスではすぐ上限に近づきます。`printDeviceInfo()` / `printAllDeviceInfo()` やendpoint/channel診断APIで使用量を確認してください。
- **USB Hub:** 複数デバイスの確認にはセルフパワーのUSB 2.0 Hubを推奨します。USB 3.x Hubや内部多段Hubは挙動が複雑で、十分に検証できていません。
- **USB Audio:** 標準Arduino `USBAudioCard`（UAC1）と `EspUsbDevice` peer（UAC2）で入力・出力のpeerテストは通っています。実USBマイク・オーディオIFでの確認はまだ限定的です。またESP32-S3/S2のホストはfull-speed専用のため、full-speed configurationを持たないUAC2デバイスはそもそも列挙できません。Clock Selector / Clock Multiplierや高度なAudio Control Unitは非対応です。
- **UVC / USBカメラ:** 現在は非対応です。Arduino-ESP32のプリコンパイル済みUSB Hostスタックは`CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256`でビルドされており、Configuration Descriptorが256 bytesを超えるUSBデバイスはクラスドライバを開始する前の列挙で失敗します。実機確認したLogitech C920のDescriptorは1974 bytesでした。この値はスケッチやEspUsbHostのビルドオプションでは変更できず、Arduino-ESP32のプリコンパイルライブラリを生成する設定の制限です。この制限がArduino-ESP32側で解消された場合にUVC対応を改めて検討します。
- **Mass Storage:** FAT利用は実用上単一MSCデバイスを前提にしてください。複数MSC、複数LUN、特殊なblock size、異常系BOT復旧は追加検証が必要です。非準拠デバイスではMSC節の`SYNCHRONIZE CACHE(10)`フォールバックが必要になる場合があります。
- **活線挿抜:** ファイル、シリアル転送、Audio stream、各クラス操作の途中で抜くと、デバイスによっては失敗やデータ喪失が起こります。
- **ESP32-P4:** `EspUsbHostConfig::port`でFS/HS OTGを選べますが、P4向け検証は継続中です。特にHS OTGとHubの組み合わせには実用上の制限があります。

## ハードウェア要件

- ESP32-S3、またはArduino-ESP32 USB Hostに対応したボード
- Arduino-ESP32コア

### ESP32-S3 の推奨機材と注意事項

USB Hostとして使用する前に、まずボードのUSB端子からVBUS（5V）が供給されているかを確認してください。

Espressif Systems純正のESP32-S3-DevKitC-1は、USB OTG端子から接続機器へ電源を供給しません。USB Deviceとして使用する場合はボード側から給電しない構成が好都合ですが、USB Hostとして使用する場合は、接続するUSB機器へ別途電源を配線するか、外部電源に対応したセルフパワーUSBハブを使用してください。

M5Stack系製品には、プログラムからUSB端子への電源供給を制御できるものもあります。使用する製品の回路図と電源制御方法を確認してください。

USB Hostを手軽に試す場合は、USB Type-CのUSB OTG端子から接続機器へ給電できるFreenove社のESP32-S3-WROOM Boardなどを推奨します。

最終的には、AtomS3のようにUSB端子が1つだけの製品でも利用できます。ただし開発中は、書き込みやSerial Monitorに使うUART端子と、接続機器に使うUSB OTG端子を分けられる、USB端子を2つ搭載したボードを推奨します。

USB端子が2つある場合でも、どちらがUSB-to-UARTで、どちらがESP32-S3のUSB OTGへ接続されているかはボードによって異なります。たとえばEspressif Systems純正のESP32-S3-DevKitC-1とFreenove ESP32-S3-WROOM Boardでは、UART端子とUSB OTG端子の配置が逆です。コネクタの位置だけで判断せず、ボード上のシルク印刷、製品資料、回路図を確認してください。

### ESP32-P4 の注意事項

ESP32-P4はSoC内部に3つのUSB機能を持っています。これはSoC内部のcontroller/PHY経路を数えたもので、すべてのボードに必ず3個の物理コネクタがあるという意味ではありません。

1. **USB Serial/JTAG** — 書き込み、console CDC、JTAGに使う固定機能のFull-speed USB controllerです。
2. **USB OTG FS** — HostまたはDeviceとして使用できる、プログラム可能なFull-speed/Low-speed OTG controllerです。
3. **USB OTG HS** — USB専用ピンを使用し、HostまたはDeviceとして使用できる、プログラム可能なHigh-speed OTG controllerです。

実際のボードは3機能すべてを個別に引き出すこともあれば、一部だけを引き出したり、1つのコネクタの役割を切り替えたりします。さらにCH34xやCP210xなどの外付けUSB-UART変換ICが追加されることがあります。外付けUSB-UARTはP4内蔵USB controllerではないため、ボード上ではUSB関連のコネクタ/経路が4系統に見える場合があります。代表的な組み合わせは次のとおりです。

- USB OTG HS + USB OTG FS + 内蔵USB Serial/JTAG
- USB OTG HS + 内蔵FS USB Serial/JTAG + 外付けUSB-UARTシリアル
- USB OTG HS + USB OTG FS + 内蔵USB Serial/JTAG + 外付けUSB-UARTシリアル

`USB`、`OTG`、`UART`、`DOWNLOAD`などのコネクタ表記だけで判断せず、必ずボード回路図を確認してください。外付けUSB-UARTコネクタの先はP4の通常のUARTであり、内蔵USB Serial/JTAGやUSB OTG controllerとは独立しています。

SoCレベルの信号ピンは以下です。実際のボードでどのコネクタに配線されているかはボード設計によって異なるため、配線やポート選択の前に回路図を確認してください。

| ESP32-P4での典型的な役割 | D- | D+ | 備考 |
|--------------------------|----|----|------|
| USB CDC FS / USB Serial/JTAG FS | GPIO24 | GPIO25 | 内蔵USB Serial/JTAG、またはFSデバイス側CDCのコネクタとして使われることが多いペアです。ボードによってはUSB Host用コネクタと混同しやすいです。 |
| USB OTG FS | GPIO26 | GPIO27 | Full-speed OTGコネクタとして使われることが多いペアです。USB Hostでは`ESP_USB_HOST_PORT_FULL_SPEED`で選択します。 |
| USB OTG HS | package pin 49 | package pin 50 | High-speed OTGポート。汎用GPIOではなくUSB専用ピンです。`ESP_USB_HOST_PORT_HIGH_SPEED`で選択します。 |

ESP32-P4にはFull-speed/Low-speed PHYが2つあります。USB OTG FSとUSB Serial/JTAGは互いに別のPHYへ接続されます。

| 割り当て | GPIO24/GPIO25（FSLS PHY0） | GPIO26/GPIO27（FSLS PHY1） |
|----------|-----------------------------|-----------------------------|
| デフォルト | USB Serial/JTAG | USB OTG FS |
| 入れ替え後 | USB OTG FS | USB Serial/JTAG |

この割り当ては`USB_PHY_SEL` eFuseで恒久的に入れ替えることも、実行時に一時的に入れ替えることもできます。これは一対一の交換であり、信号の複製ではありません。USB OTG FSとUSB Serial/JTAGは常に別々のFSLS PHYを使用します。**eFuseへの書き込みは不可逆であり、特定ボードのコネクタへ対応するためだけに変更することは推奨しません。** 開発中やボード固有ファームウェアでは、通常は実行時のソフトウェア上書きの方が安全です。

ESP-IDFはP4用Low-level HALで実行時の割り当て変更を提供しています。`usb.begin()`および他のUSB OTG FS driverを初期化する前に呼び出します。

```cpp
#include "hal/usb_wrap_ll.h"
#include "soc/usb_wrap_struct.h"

// USB OTG FSをGPIO24/GPIO25へ接続する。USB Serial/JTAGはGPIO26/GPIO27へ移動する。
usb_wrap_ll_phy_select(&USB_WRAP, 0);

EspUsbHostConfig config;
config.port = ESP_USB_HOST_PORT_FULL_SPEED;
usb.begin(config);
```

`usb_wrap_ll_phy_select(&USB_WRAP, 0)`はUSB OTG FSをFSLS PHY0（GPIO24/GPIO25）へ接続し、同時にUSB Serial/JTAGをFSLS PHY1（GPIO26/GPIO27）へ接続します。`1`を指定すると反対の割り当てになります。同じUSB機能が両方のピンペアへ出力されるわけではありません。これはFS PHY割り当てのソフトウェア上書きであり、eFuseには書き込みません。チップをリセットすると、起動時のeFuse/デフォルト割り当てへ制御が戻ります。

USB Serial/JTAGをGPIO26/GPIO27へ移しても、新しいCDC stackを生成・初期化するわけではありません。内蔵の固定機能USB Serial/JTAG controllerを移動するため、そのcontrollerが有効ならGPIO26/GPIO27側でCDC/JTAG USB deviceとして動作できます。GPIO24/GPIO25経由でSerial Monitor、書き込み、JTAG debugを行っていた場合、OTG FSをGPIO24/GPIO25へ移した時点でその接続は切断されます。切り替え後もログが必要な場合は、外付けUSB-UARTコネクタなど別のconsoleを使うか、GPIO26/GPIO27側へ移動したUSB Serial/JTAGへ接続してください。

OTG FSのHostまたはDevice driverが動作中に割り当てを変更してはいけません。別のframeworkが先にOTG FSを初期化している場合は、そのdriverを停止・uninstallしてから経路を変更し、その後で`EspUsbHost`を初期化します。別のFS stackを開始していない通常のスケッチなら、`usb.begin(config)`の直前に呼べば十分です。USB Serial/JTAGはROMやArduino coreによって先に初期化されている場合があり、切り替えると旧PHY側では物理的なUSB切断、新PHY側では再列挙が発生する可能性があります。

この設定で変わるのはD+/D-の信号経路だけです。USB HostにはVBUS供給、過電流保護、USB-Cの場合はrole/CC制御も必要であり、これらはボード側ハードウェアが提供する必要があります。GPIO24/GPIO25へ配線されたコネクタであっても、PHYを切り替えるだけで電気的にHostとして使用可能になるとは限りません。またUSB Serial/JTAGがGPIO26/GPIO27へ移るため、26/27側コネクタがHost用VBUSを出力したままになっていないこと、および移動後のUSB Serial/JTAGのDevice roleと競合する機器が接続されていないことを確認してください。

USB Serial/JTAGをGPIO26/GPIO27へ割り当てている間は、この2ピンを通常のGPIOや別peripheralと併用できません。USB経路を変更する前に、GPIOまたは対象peripheralの使用を停止してください。後からUSB割り当てを元へ戻してGPIO26/GPIO27を別用途へ戻す場合は、`pinMode()`を再実行するか、対象peripheral driverの`begin()`/設定APIを呼び直してピンを再初期化してください。USB PHYの初期化によってpin mux、入出力方向、pull設定が変更される可能性があるため、それ以前のGPIO/peripheral初期化が維持されているとは限りません。

USB Hostとして使えるのはOTGポートです。ボードによっては、どのコネクタがFS OTGで、どれがCDC/デバイス用なのか分かりにくい場合があります。ボードの回路図とサンプル設定を確認してください。

ESP-IDFのUSB Hostスタックは、同時に1つのHost peripheralしか利用できません。ESP32-P4では、`EspUsbHostConfig::port`でFS OTGまたはHS OTGのどちらかを選択します。FS OTGとHS OTGを同時にUSB Hostとして使うことはできません。Arduinoライブラリとして使う場合、USBデバイス機能はHS側を使用し、FS側をデバイス機能として選択することはできません。

ESP32-P4ではUSB転送に使うDMA可能メモリがキャッシュされます。ESP-IDFのHost driverはIN用bufferを転送完了後にinvalidateするだけで、DMA開始前のwrite backを行わないため、allocatorが残したdirty cache lineがコントローラの書き込み中にevictされ、受信データを上書きすることがあります。本ライブラリはIN転送のsubmit直前に`esp_cache_msync()`でwrite backするため、アプリ側の対処は不要です。実機での確認は`tests/manual/msc_cache_coherency`で行えます。

現時点ではHS OTGの実用上の制限もあります。現在の環境では、HS OTGでUSBハブは実質的に利用できません。このライブラリではESP32-P4のUSB HostでUSBキーボードが動作することは確認済みですが、ESP32-P4向けの詳細な検証はまだ十分ではありません。

## インストール

Arduino IDEのライブラリマネージャーで **EspUsbHost** を検索してインストール。

またはArduinoの`libraries/`フォルダへクローン：

```sh
git clone https://github.com/tanakamasayuki/EspUsbHost
```

## クイックスタート

```cpp
#include "EspUsbHost.h"

EspUsbHost usb;

void setup() {
  Serial.begin(115200);

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
    if (event.pressed && event.ascii) {
      Serial.print((char)event.ascii);
    }
  });

  if (!usb.begin()) {
    Serial.printf("usb.begin() failed: %s\n", usb.lastErrorName());
  }
}

void loop() {
}
```

## サンプル一覧

### HID

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostKeyboard](examples/HID/EspUsbHostKeyboard/) | キーボード入力を受け取り、入力文字をシリアルに出力 |
| [EspUsbHostKeyboardDump](examples/HID/EspUsbHostKeyboardDump/) | パース済みキーボードイベントを表示し、`onKeyboard` の自前処理を示す |
| [EspUsbHostKeyboardNKRO](examples/HID/EspUsbHostKeyboardNKRO/) | N-key rollover キーボードをホストし、同時押し数をカウント |
| [EspUsbHostMouse](examples/HID/EspUsbHostMouse/) | マウスの移動量とボタン操作を取得 |
| [EspUsbHostCompositeHID](examples/HID/EspUsbHostCompositeHID/) | キーボード+マウスなどの複合HIDデバイスを扱う |
| [EspUsbHostConsumerControl](examples/HID/EspUsbHostConsumerControl/) | メディアキー（音量・再生/一時停止など）を検出 |
| [EspUsbHostSystemControl](examples/HID/EspUsbHostSystemControl/) | システムキー（電源・スタンバイ・ウェイクアップ）を検出 |
| [EspUsbHostGamepad](examples/HID/EspUsbHostGamepad/) | ゲームパッドのスティック・十字キー・ボタンを取得 |
| [EspUsbHostHIDVendor](examples/HID/EspUsbHostHIDVendor/) | ベンダーHID入力と出力/フィーチャーレポートの送信 |
| [EspUsbHostHIDRawDump](examples/HID/EspUsbHostHIDRawDump/) | デバイスアドレス付きでHexダンプ（複数デバイス対応） |
| [EspUsbHostDp100Power](examples/HID/EspUsbHostDp100Power/) | ALIENTEK DP100電源を読み取る。64バイトHIDレポートに載った独自フレーム（CRC-16/MODBUS） |
| [EspUsbHostMacroPadN3](examples/HID/EspUsbHostMacroPadN3/) | Mirabox N3 / Ajazz AKP03系のLCDマクロパッドを駆動する。6キーの画面描画とキー入力（ESP32-P4限定） |

### Info

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostBringUpCheck](examples/Info/EspUsbHostBringUpCheck/) | 新しいボードで最初に動かすツール。ホストが起動したか、何か列挙されたか、速度は何か。列挙されない場合のチェックリスト付き |
| [EspUsbHostDeviceExplorer](examples/Info/EspUsbHostDeviceExplorer/) | 未知のデバイスの識別。インターフェースごとの「何であり、どのAPIで扱うか」と、生ディスクリプタのバイト列およびブロック単位の走査 |
| [EspUsbHostDeviceInfo](examples/Info/EspUsbHostDeviceInfo/) | 接続中の全デバイスのディスクリプタ・インターフェース・エンドポイントを表示 |
| [EspUsbHostHIDReportDescriptor](examples/Info/EspUsbHostHIDReportDescriptor/) | HID調査用にHIDレポートディスクリプタと簡易item decodeを表示 |
| [EspUsbHostCustomDeviceCallbacks](examples/Info/EspUsbHostCustomDeviceCallbacks/) | 接続・切断コールバックを自分で定義し、接続デバイスを調べる |
| [EspUsbHostHubPPPS](examples/Info/EspUsbHostHubPPPS/) | PPPS対応USBハブのポート電源を制御 |
| [EspUsbHostP4FsPhyRouting](examples/Info/EspUsbHostP4FsPhyRouting/) | ESP32-P4のUSB OTG FSを実行時にGPIO24/GPIO25側USBコネクタへ切り替える |

### MIDI

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostMIDI](examples/MIDI/EspUsbHostMIDI/) | USB MIDI入出力 |

### Audio

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostAudioInput](examples/Audio/EspUsbHostAudioInput/) | USB AudioのIsochronous INペイロードを受信 |
| [EspUsbHostAudioOutputTone](examples/Audio/EspUsbHostAudioOutputTone/) | 簡単なトーンを生成してUSB Audio OUTへ送信 |
| [EspUsbHostAudioOutputHardwareVolume](examples/Audio/EspUsbHostAudioOutputHardwareVolume/) | USB Audio Feature Unitのmuteとハードウェアボリューム対応を確認 |
| [EspUsbHostAudioOutputMP3PCMFlow](examples/Audio/EspUsbHostAudioOutputMP3PCMFlow/) | PCMFlowで埋め込みMP3素材をデコードしてUSB Audio OUTへ再生 |
| [EspUsbHostAudioOutputMP3ESP8266Audio](examples/Audio/EspUsbHostAudioOutputMP3ESP8266Audio/) | ESP8266Audioで埋め込みMP3素材をデコードしてUSB Audio OUTへ再生 |

### Serial

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostUSBSerial](examples/Serial/EspUsbHostUSBSerial/) | CDC ACM・VCPシリアルの双方向ブリッジ |
| [EspUsbHostMultiUSBSerial](examples/Serial/EspUsbHostMultiUSBSerial/) | FTDIとCP210xのUSBシリアルデバイスを同時利用 |
| [EspUsbHostDisplayTuring](examples/Serial/EspUsbHostDisplayTuring/) | 3.5インチUSBスマートスクリーン（CDCシリアルプロトコル）をLovyanGFXのpanelとして駆動。LGFXVirtualCanvasの差分転送を利用 |

### Storage

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostMSCBlockDump](examples/Storage/EspUsbHostMSCBlockDump/) | MSCの容量情報を表示し、先頭ブロックをダンプ |
| [EspUsbHostMSCFatList](examples/Storage/EspUsbHostMSCFatList/) | MSCをArduino `fs::FS`としてマウントし、ファイル一覧と小さなwrite/read/delete確認を行う |

### CCID

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostCcidReader](examples/Ccid/EspUsbHostCcidReader/) | CCIDスマートカードリーダーをopenし、カードの挿入・排出を通知、ATRとカード種別を読み、PC/SCのGet UID APDUを送る |
| [EspUsbHostCcidFelicaIdm](examples/Ccid/EspUsbHostCcidFelicaIdm/) | Sony RC-S300でSystem Codeを指定してFeliCaのIDmを読む。transparent sessionでRFフィールドを奪い、FeliCa Pollingフレームを自分で送る。リーダー自前のワイルドカードポーリングが捕まえたものではなく、特定のシステムに届かせる唯一の方法 |

### Network

| スケッチ | 説明 |
|----------|------|
| [UsbNetwork](examples/UsbNetwork/) | CDC-NCM/ECMのUSB EthernetアダプタをDHCPクライアントのlwIP netifとして立ち上げ、USB経由で`HTTPClient` GETを実行。接続時に全configurationのCDC-ECM/NCM候補を表示 |

### Vendor

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostProtocolConsole](examples/Vendor/EspUsbHostProtocolConsole/) | 仕様非公開のプロトコル解析用の対話コンソール。シリアルモニタからコントロール／バルク転送を打ち込み、応答を確認する |
| [EspUsbHostVendorBulk](examples/Vendor/EspUsbHostVendorBulk/) | 汎用の非HID vendor-specificインターフェース：bulk IN/OUTとEP0 vendor control IN/OUT |
| [EspUsbHostAdbConnect](examples/Vendor/EspUsbHostAdbConnect/) | 汎用vendor bulk API上でAndroid ADB認証と単一shell streamを実行 |
| [EspUsbHostDisplayDl1xx](examples/Vendor/EspUsbHostDisplayDl1xx/) | USBグラフィックスアダプタ（DL-1xx bulkプロトコル）をLovyanGFXのpanelとして駆動。LGFXVirtualCanvasでFull HD面を扱う |
| [EspUsbHostDisplayAx206](examples/Vendor/EspUsbHostDisplayAx206/) | AX206のUSBフォトフレームディスプレイ（vendorコマンドのBulk-Only Transport）をLovyanGFXのpanelとして駆動。フレームバッファを持たず、1トランザクションで1フレームをストリーム |
| [EspUsbHostUsbtmcScpi](examples/Vendor/EspUsbHostUsbtmcScpi/) | USBTMC計測器（class 0xFE）にSCPIで話す。EP0のclass request、bulkメッセージ層、菊水PMX電源のラッパー |
| [EspUsbHostPrinterEscPos](examples/Vendor/EspUsbHostPrinterEscPos/) | ESC/POSレシートプリンタ（class 0x07）で印字する。EP0のclass request、リアルタイムステータス、日本語レシート＋バーコード＋QR＋カット |

USBディスプレイ関連のexampleは [docs/usb-display.ja.md](docs/usb-display.ja.md) にまとめてあります。

## APIリファレンス

### コア

```cpp
bool begin();
bool begin(const EspUsbHostConfig &config);
void end();
bool ready() const;
bool setConfigurationSelector(ConfigurationSelector selector);
```

`setConfigurationSelector()`は`begin()`より前に登録し、渡されたdevice descriptorに対して
activeにするconfiguration値を返します（`0`はdevice既定値を維持）。列挙中にUSB Host task上で
実行されるためブロックしてはいけません。Arduino-ESP32 3.3.11以降（`enum_filter_cb`）が必要で、
それ以前のcoreでは`ESP_ERR_NOT_SUPPORTED`で`false`を返します。主に、CDC-NCM/ECMを既定以外の
configurationに持つUSB Ethernetアダプタで必要になります。

`end()`はこのインスタンスがマウントしたMSCボリュームをunmountし、client/daemon taskを
同期的に停止し、実行中のendpoint transferをcancelしてcallback返却まで待ち、clientを
deregisterし、ESP-IDFの`ALL_FREE` handshake完了後にUSB Host Libraryをuninstallします。復帰後は同じ
`EspUsbHost` objectを`begin()`で再開できます。`end()`はUSB event/data
callback内ではなくapplication taskから呼び出してください。

`EspUsbHostConfig`でバックグラウンドタスクのスタックサイズ・優先度・コアを調整できます：

```cpp
struct EspUsbHostConfig {
  uint32_t    taskStackSize = 8192;
  UBaseType_t taskPriority  = 5;
  BaseType_t  taskCore      = tskNO_AFFINITY;
  EspUsbHostPort port       = ESP_USB_HOST_PORT_DEFAULT;
  EspUsbHostFifoConfig fifo = {};
};
```

ESP32-P4で特定のOTG peripheralを選びたい場合は、`port`に`ESP_USB_HOST_PORT_FULL_SPEED`または`ESP_USB_HOST_PORT_HIGH_SPEED`を指定します。他のチップではこの設定は無視されます。

#### エンドポイントサイズの上限（`fifo`）

Host controllerはパケットをハードウェアFIFOに一時保存し、その領域を3分割して使います。この分割が、hostが開けるエンドポイントの最大サイズを決めます。

| エンドポイント | 上限 | High-speedポートの既定値 |
| --- | --- | --- |
| IN（転送種別を問わず） | `(rxFifoLines - 2) * 4` | 約2400バイト |
| Control / bulk OUT | `nptxFifoLines * 4` | 1024バイト |
| Interrupt / isochronous OUT | `ptxFifoLines * 4` | **512バイト** |

512バイトを超えるinterrupt OUTエンドポイントを持つデバイス（Stream Deck系マクロパッドなどのhigh-speed vendor HIDは1024バイトを使います）はclaimに失敗して`ESP_ERR_NOT_SUPPORTED`となり、host driverが`HCD DWC: EP MPS (1024) exceeds supported limit (512)`を出力します。FIFOを再分割して領域を確保してください。

```cpp
EspUsbHostConfig config;
config.port = ESP_USB_HOST_PORT_HIGH_SPEED;
config.fifo = ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT;  // {260, 128, 280} lines
usb.begin(config);
```

`ESP_USB_HOST_FIFO_LARGE_PERIODIC_OUT`は、512バイトのbulk転送を使えるまま1024バイトのinterrupt OUTエンドポイントを許可します。別の配分にしたい場合は3つのフィールドを直接指定します。単位は4バイトのline、`rxFifoLines`と`nptxFifoLines`は0にできず、合計はポートの容量に収める必要があります（ESP32-P4のhigh-speedポートは1024 line＝4 kB、full-speedポートは256 line＝1 kB）。1024バイトのエンドポイントがhigh-speedポートでしか開けないのはこのためです。`fifo`を既定値のままにすればdriver側の分割を使います。arduino-esp32 3.3.0以降が必要で、それ以前のコアでは警告を出して既定の分割を使います。

### デバイスイベント

```cpp
void onDeviceConnected(DeviceCallback callback);
void onDeviceDisconnected(DeviceCallback callback);
EspUsbHostListenerId addDeviceConnectedListener(DeviceCallback callback);
EspUsbHostListenerId addDeviceDisconnectedListener(DeviceCallback callback);
bool removeListener(EspUsbHostListenerId listenerId);
void espUsbHostPrint(const EspUsbHostDeviceInfo &device, Print &out = Serial);
```

コールバックは`const EspUsbHostDeviceInfo &device`を受け取ります。主要フィールド：`address`、`vid`、`pid`、`product`、`manufacturer`、`serial`、`speed`、`parentAddress`、`portId`。
`espUsbHostPrint(device)`は1行サマリを出力します。`connected:`や`disconnected:`などのイベント文脈はコールバック側で付けてください。

`portId`はデバイスの接続位置を表します。`0x01`はルートポート直結です。ハブ配下のデバイスでは上位ニブルが検出順に割り当てられたハブ番号、下位ニブルがそのハブのポート番号です。例えば`0x12`は「ハブ#1のポート2」を表します。

listenerの契約は後述の[HID入力](#hid入力)のlistenerと同じです。ただし上限は独立で、
既定8件（`EspUsbHost::MaxLifecycleListeners`、`ESP_USB_HOST_MAX_LIFECYCLE_LISTENERS`で変更可）です。
lifecycleはデバイスを追跡するすべてのサブシステムが購読するため、必要数は単一の入力eventのように
頭打ちにならず、stack上に載せたサブシステムの数に比例して増えるためです。
接続eventは未サポートのデバイスでも発火するので、listenerはデバイスと通信できると仮定する前に
`device.supported`を確認してください。

### HID入力

```cpp
void onKeyboard(KeyboardCallback callback);
void onKeyboardState(KeyboardStateCallback callback);
void onMouse(MouseCallback callback);
void onConsumerControl(ConsumerControlCallback callback);
void onSystemControl(SystemControlCallback callback);
void onGamepad(GamepadCallback callback);
void onHIDInput(HIDInputCallback callback);    // 生データ — 全HIDインターフェースで発火
void onHIDVendorInput(HIDVendorInputCallback callback);
EspUsbHostListenerId addKeyboardListener(KeyboardCallback callback);
EspUsbHostListenerId addKeyboardStateListener(KeyboardStateCallback callback);
EspUsbHostListenerId addMouseListener(MouseCallback callback);
EspUsbHostListenerId addConsumerControlListener(ConsumerControlCallback callback);
EspUsbHostListenerId addSystemControlListener(SystemControlCallback callback);
EspUsbHostListenerId addGamepadListener(GamepadCallback callback);
bool removeListener(EspUsbHostListenerId listenerId);
void espUsbHostPrint(const EspUsbHostHIDInput &input, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostKeyboardEvent &event, Print &out = Serial);
const char *espUsbHostConsumerControlUsageName(uint16_t usage);
const char *espUsbHostSystemControlUsageName(uint8_t usage);
```

互換性のため、各`on*()`は従来どおり単一callbackを保持します。対応する
`add*Listener()`を使うと、アダプタとスケッチが互いを上書きせず、同じパース済みHIDイベントを
受信できます。登録成功時は0以外の`EspUsbHostListenerId`を返します。0
（`ESP_USB_HOST_INVALID_LISTENER_ID`）は空callbackまたはeventのlistener上限到達を表します。
`removeListener()`はHID入力・[デバイスlifecycle](#デバイスイベント)・[MIDI](#midi-1)の
どのlistener IDも受け取り、解除できたかを返します。

listenerはeventごとに既定4件（`EspUsbHost::MaxListenersPerEvent`）で、コンパイル時に
`ESP_USB_HOST_MAX_LISTENERS_PER_EVENT`で変更できます。単一`on*()` callbackを最初に呼び、
続いてlistenerを登録順に呼びます。callback集合はeventごとにsnapshotするため、callback内での
追加・解除は次のeventから反映されます。スケッチtaskとUSB task間の登録・差し替え・解除は
保護され、利用者callbackの実行中はregistryのmutexを保持しません。

```cpp
EspUsbHostListenerId adapterListener =
    usb.addKeyboardListener([](const EspUsbHostKeyboardEvent &event) {
      // アダプタ側の入力処理
    });

usb.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
  // スケッチ側の入力処理。両方が同じeventを受け取る
});

// 後でtask contextから解除
usb.removeListener(adapterListener);
```

6キーの boot キーボードと N-key rollover（NKRO）キーボードの両方に対応します。NKRO
キーボードはキーをビットマップ（report protocol）で送るため同時押し数に制限がありません。
ホストは HID report descriptor からレポートレイアウトを学習して自動でデコードするので、
`onKeyboard` はどちらでも同じ press/release イベントを返します。`keyboardUsesBitmapReport(address)`
で検出したフォーマット（診断用）を確認できます。[KeyboardNKRO](examples/HID/EspUsbHostKeyboardNKRO/) 例を参照。

`onKeyboardState`はキーボードreportで状態が変化するたびに、入力形式に依存しないスナップショットを
1回通知します。`bitmap`はKeyboard/Keypad usage `0x00～0xFF`の現在状態、`changedBitmap`は
そのreportで変化したusageです。`isDown(keycode)`、`wasPressed(keycode)`、
`wasReleased(keycode)`で確認できます。修飾キーも通常キーと同じusage `0xE0～0xE7`として
含まれるため、修飾キー単独の変化も取得できます。boot、Report ID付きboot、NKROのすべてで
同じAPIを使用し、状態が変化しないreportではコールバックを呼びません。

```cpp
usb.onKeyboardState([](const EspUsbHostKeyboardState &state) {
  for (uint16_t usage = 0; usage <= 0xff; usage++) {
    uint8_t keycode = static_cast<uint8_t>(usage);
    if (state.wasPressed(keycode)) {
      // Left Ctrl (0xE0)などの修飾キーもkeycodeに含まれる
    }
    if (state.wasReleased(keycode)) {
      // このreportで離された
    }
  }
});
```

主なイベントフィールド：

パース済みHIDコールバック（`onKeyboard`、`onKeyboardState`、`onMouse`、`onConsumerControl`、`onSystemControl`、`onGamepad`、`onHIDVendorInput`）はすべて、`vid`、`pid`、`manufacturer`、`product`、`serial`、入力レポート全体を指す`rawData` / `rawLength`、Report IDがある場合にそれを除いたレポートバイトを指す`reportData` / `reportLength`を含みます。

| コールバック | 主要フィールド |
|-------------|--------------|
| `onKeyboard` | `pressed`、`keycode`、`ascii`、`modifiers`、`address` |
| `onKeyboardState` | `bitmap`、`changedBitmap`、`modifiers`、`isDown()`、`wasPressed()`、`wasReleased()`、`address` |
| `onMouse` | `x`、`y`、`wheel`、`pan`、`buttons`、`previousButtons`、`buttonMask`、`previousButtonMask`、`buttonCount`、`moved`、`buttonsChanged`、`address` |
| `onConsumerControl` | `pressed`、`usage`（16ビットHIDユーセージ）、`address` |
| `onSystemControl` | `pressed`、`usage`（8ビット）、`address` |
| `onGamepad` | `fields`、`fieldCount`、`rawData`、`reportData`、`vid`、`pid`、`address` |
| `onHIDInput` | `address`、`vid`、`pid`、`interfaceNumber`、`subclass`、`protocol`、`data`、`length` |

代表的なConsumer Control定数として、`ESP_USB_HOST_CONSUMER_CONTROL_PLAY_PAUSE`、`ESP_USB_HOST_CONSUMER_CONTROL_MUTE`、`ESP_USB_HOST_CONSUMER_CONTROL_VOLUME_UP`、`ESP_USB_HOST_CONSUMER_CONTROL_VOLUME_DOWN`、`ESP_USB_HOST_CONSUMER_CONTROL_NEXT_TRACK`、`ESP_USB_HOST_CONSUMER_CONTROL_PREVIOUS_TRACK`を用意しています。

### HID出力

```cpp
void setKeyboardLayout(EspUsbHostKeyboardLayout layout);
bool setKeyboardLeds(bool numLock, bool capsLock, bool scrollLock,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool getKeyboardNumLock(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool getKeyboardCapsLock(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool getKeyboardScrollLock(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool sendHIDReport(uint8_t interfaceNumber, uint8_t reportType, uint8_t reportId,
                   const uint8_t *data, size_t length,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool sendHIDVendorOutput(const uint8_t *data, size_t length,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool sendHIDVendorFeature(const uint8_t *data, size_t length,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
```

`setKeyboardLeds()` は boot キーボードに加えて、report protocol しか持たないキーボード
（report ID 付き複合 HID デバイスや NKRO キーボード）でも動作します。boot interface の
宣言がない場合は、HID report descriptor から見つけた LED output report を使い、
Set_Report にキーボードの report ID を付けて送信します。接続時にはホストの現在の
lock 状態を一度キーボードへ送信します。

lock 状態を保持しているのはライブラリ側です。Lock キーが押されるたびにトグルして LED
report を送り直すため、`getKeyboardNumLock()` / `getKeyboardCapsLock()` /
`getKeyboardScrollLock()` はコールバックを待たずにいつでも現在値を返します。lock 状態は
キーボードごとに持つので、複数台つないでいる場合は `address` を指定してください。同じ値は
`onKeyboard` と `onKeyboardState` の通知にも含まれます。キーボードが未接続のときは3つとも
false を返します。

`sendHIDVendorOutput()` と `sendHIDVendorFeature()` は HID vendor report 用です。HIDではない vendor-specific interface には次の Vendor bulk/control API を使います。

デフォルトは`ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US`です。以下のいずれかの定数を`setKeyboardLayout()`に渡します：

| 定数 | ロケール | 備考 |
|------|----------|------|
| `ESP_USB_HOST_KEYBOARD_LAYOUT_ZH_TW` | zh_TW | 繁体字中国語 — 記号はUS QWERTYと同一 |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_DA_DK` | da_DK | デンマーク語 |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_DE_DE` | de_DE | ドイツ語 QWERTZ |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US` | en_US | 英語 US（**デフォルト**） |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_FI_FI` | fi_FI | フィンランド語 |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_FR_FR` | fr_FR | フランス語 AZERTY |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_HU_HU` | hu_HU | ハンガリー語 QWERTZ |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_IT_IT` | it_IT | イタリア語 |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP` | ja_JP | 日本語 |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_KO_KR` | ko_KR | 韓国語 — 記号はUS QWERTYと同一 |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_NL_NL` | nl_NL | オランダ語 |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_NB_NO` | nb_NO | ノルウェー語（ブークモール） |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_PT_BR` | pt_BR | ブラジルポルトガル語 ABNT2 |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_SV_SE` | sv_SE | スウェーデン語 |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_ZH_CN` | zh_CN | 簡体字中国語 — 記号はUS QWERTYと同一 |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_EN_GB` | en_GB | 英語 UK |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_PT_PT` | pt_PT | ポルトガル語（ポルトガル） |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_ES_ES` | es_ES | スペイン語 |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_FR_CH` | fr_CH | スイスフランス語 QWERTZ |

`event.ascii`はLatin-1エンコードの`uint8_t`（0x00〜0xFF）です。デッドキー（´、\`、^、~、¨）およびLatin-1の範囲外の文字は`ascii = 0`になります。`ZH_TW`・`KO_KR`・`ZH_CN`の記号配列は`EN_US`と同一で、実際のCJK文字入力はホスト側のIMEが必要です。

### USBシリアル（CDC ACM・VCP）

`EspUsbHost`の低レベル送信API：

```cpp
bool sendSerial(const uint8_t *data, size_t length,
                uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool sendSerial(const char *text,
                uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool serialReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool setSerialBaudRate(uint32_t baud,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool setSerialConfig(const EspUsbHostSerialConfig &config,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
uint16_t serialOutPacketSize(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
```

`sendSerial()` は完了を待ちません。呼び出しごとに転送を確保してドライバへ渡すだけです。ターミナル程度の流量なら問題ありませんが、endpointより速く書き続けるとin-flightが増え続けてDMAメモリを食い潰します。有限に抑える形が非同期CDC OUTキューで、vendor bulkのものと同じ形をしています。

```cpp
bool serialWriteQueueBegin(size_t depth, size_t bufferBytes,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void serialWriteQueueEnd(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool serialWriteQueueReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

uint8_t *serialWriteAcquire(size_t *capacity, uint32_t timeoutMs = 0,
                            uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool serialWriteSubmit(uint8_t *buffer, size_t length,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void serialWriteRelease(uint8_t *buffer, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool serialWriteAsync(const uint8_t *data, size_t length, uint32_t timeoutMs = 0,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

size_t serialWritePending(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t serialWriteQueueFree(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool serialWriteFlush(uint32_t timeoutMs, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
EspUsbHostSerialWriteStats serialWriteStats(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
void serialWriteStatsReset(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
```

`serialWriteQueueBegin()` は `bufferBytes` サイズの再利用可能な transfer を `depth` 個だけ事前確保します（depthの上限は `ESP_USB_HOST_SERIAL_WRITE_QUEUE_MAX_DEPTH`）。submitは待ちませんが、プールが埋まると `serialWriteAcquire()` が `timeoutMs` までブロックします。この待ちが押し戻しです。キューが有効な間は `sendSerial()` と `EspUsbHostCdcSerial::write()` もこのキューを通るので、既存コードもそのままこのペース制御を受けます（スロットサイズを超える書き込みは従来の単発経路のままです）。`EspUsbHostCdcSerial::flush()` はキューが有効なときだけドレインを待ちます。`serialWriteFlush()` は完了コールバックが動く場所であるUSB client taskからは呼べません。

動機となった実例が [`examples/Serial/EspUsbHostDisplayTuring`](examples/Serial/EspUsbHostDisplayTuring/) です。CDC経由のUSBディスプレイで、1フレームが300 KBあり、書き手をバスに追従させているのはこのキューだけです。

`EspUsbHostCdcSerial`は標準Arduino `Stream`/`Print`ラッパーです：

```cpp
EspUsbHostCdcSerial CdcSerial(usb);

bool    setRxBufferSize(size_t size);
size_t  rxBufferSize() const;
bool    begin(uint32_t baud = 115200);
void    end();
bool    connected() const;
int     available();
int     read();
int     peek();
void    flush();
size_t  write(uint8_t data);
size_t  write(const uint8_t *buffer, size_t size);
bool    setBaudRate(uint32_t baud);
bool    setConfig(const EspUsbHostSerialConfig &config);
bool    setDtr(bool enable);
bool    setRts(bool enable);
void    setAddress(uint8_t address);
uint8_t address() const;
void    clearAddress();
```

受信バイトは、USB client taskが書き込み`read()`が読み出すリングバッファに入ります。既定は512バイトで、溢れると最も古いバイトを黙って捨てるため、`read()`が呼ばれない時間がリングの容量を超えるとデータを失います。921600 baudでは512バイトは約5.5ms分でしかなく、バースト的なデバイス（1秒分のNMEAをまとめて出すGPS、起動時のログダンプなど）は平均レートが低くても超えることがあります。`setRxBufferSize()`はインスタンス毎にリングサイズを指定します。attach中はUSB client taskが書き込んでいるため、`begin()`の前（または`end()`の後）に呼ぶ必要があります。attach中・`size`が2未満・確保失敗のいずれかで`false`を返します：

```cpp
void setup() {
  CdcSerial.setRxBufferSize(8192);
  CdcSerial.begin(115200);
}
```

通常は`setRxBufferSize()`を使ってください。コンパイル時の既定値を変更する方法は**非推奨**です。渡し方が環境（Arduino IDE・arduino-cli・PlatformIO）ごとに異なり、全インスタンスに一律で効いてしまい、ビルドキャッシュが残っていると「変えたのに効かない」状態になるためです。`setRxBufferSize()`を呼べない構成でのみ使ってください。

必要な場合は、`.ino`と同じスケッチフォルダに`build_opt.h`というファイルを置き、そこにフラグを書きます。このファイルはArduinoのビルドが全翻訳単位に渡します。`.ino`内の`#define`ではこれができません。スケッチとライブラリは別の翻訳単位としてコンパイルされるため、スケッチ側の定義は片側にしか届かず、クラスのレイアウトが片側だけ変わるODR違反（リンクは通るのに実行時に壊れる）になります。

```
-DESP_USB_HOST_CDC_RX_BUFFER_SIZE=8192
-DESP_USB_HOST_VENDOR_RX_BUFFER_SIZE=2048
```

拡張子が`.h`でもCのソースではありません。コンパイラにはレスポンスファイルとして渡されるため、書けるのはオプションだけです。`//`や`#`のコメント行を入れると、その行の各単語が入力ファイル名として扱われ、`cannot specify '-o' with '-c' ... with multiple files`でビルドが失敗します。

> **注意**: `build_opt.h`を編集してもビルドキャッシュが無効化されず、変更が反映されないことがあります。変更したら必ずクリーンビルドしてください。arduino-cliは`arduino-cli compile --clean`、PlatformIOは`pio run -t clean`、Arduino IDEはIDEを再起動するか、verboseビルド出力に表示されるテンポラリのビルドフォルダを削除します。またArduino IDEはコンパイル開始時点で`build_opt.h`が存在しないと認識しないため、ビルド前にファイルを作成しておく必要があります。

`ESP_USB_HOST_VENDOR_RX_BUFFER_SIZE`（既定512）はvendor bulk INのリングです。こちらはライブラリ内部のデバイス構造体に埋まっているため、実行時APIはありません。PlatformIOでは`platformio.ini`の`build_flags`で同じフラグを渡せます。この環境ごとの差こそが、この方法を非推奨としている理由です。

`EspUsbHostSerialConfig`のデフォルトは115200 8N1です。`dataBits`は5〜8ビット、`parity`は`ESP_USB_HOST_SERIAL_PARITY_NONE`、`ODD`、`EVEN`、`MARK`、`SPACE`、`stopBits`は`ESP_USB_HOST_SERIAL_STOP_BITS_1`、`1_5`、`2`を指定できます。

複数のUSBシリアルデバイスが接続されている場合は、`onDeviceConnected`内で`setAddress()`を呼び特定デバイスにバインドします。

### Vendor bulk/control

HIDではないvendor-specific interface（`bInterfaceClass == 0xff`）で、bulk endpointを持つデバイス向けのAPIです。

```cpp
void onVendorData(VendorDataCallback callback);

bool vendorOpen(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                uint8_t interfaceNumber = 0xff,
                EspUsbHostVendorReadMode readMode = ESP_USB_HOST_VENDOR_READ_CONTINUOUS);
bool vendorWrite(const uint8_t *data, size_t length,
                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
size_t vendorRead(uint8_t *buffer, size_t length,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool vendorReadSync(uint8_t *buffer, size_t length,
                    size_t *actualLength = nullptr,
                    uint32_t timeoutMs = ESP_USB_HOST_VENDOR_READ_DEFAULT_TIMEOUT_MS,
                    uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
uint16_t vendorOutPacketSize(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint16_t vendorInPacketSize(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint8_t vendorOutEndpoint(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint8_t vendorInEndpoint(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

bool vendorControlIn(uint8_t request, uint16_t value, uint16_t index,
                     uint8_t *data, size_t length,
                     size_t *actualLength = nullptr,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = ESP_USB_HOST_VENDOR_CONTROL_DEFAULT_TIMEOUT_MS);
bool vendorControlOut(uint8_t request, uint16_t value, uint16_t index,
                      const uint8_t *data = nullptr, size_t length = 0,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint32_t timeoutMs = ESP_USB_HOST_VENDOR_CONTROL_DEFAULT_TIMEOUT_MS);
bool vendorControlTransfer(uint8_t requestType, uint8_t request,
                          uint16_t value, uint16_t index,
                          uint8_t *data = nullptr, size_t length = 0,
                          size_t *actualLength = nullptr,
                          uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                          uint32_t timeoutMs = ESP_USB_HOST_VENDOR_CONTROL_DEFAULT_TIMEOUT_MS);
```

`vendorOpen()` は interface を明示的に claim し、bulk IN 受信を開始します。`interfaceNumber` が既定値のときは最初の vendor-specific（class 0xFF）interface を選びます。interface 番号を明示した場合は class を問わずその interface を claim するため、bulk プロトコルが別の class code の裏にあるデバイス（たとえば AX206 USB ディスプレイは 0xDC/0xA0/0xB0 を名乗ります）にも使えます。ただし、ライブラリの別の機能が claim 済みの interface は明示しても拒否されます。

第3引数は bulk IN endpoint の駆動方法を選びます。`ESP_USB_HOST_VENDOR_READ_CONTINUOUS`（既定値。従来の挙動）は IN 転送を常時1つ出しっぱなしにし、届いたデータを `vendorRead()` 用にバッファします。`ESP_USB_HOST_VENDOR_READ_ON_DEMAND` は転送を一切開始せず、`vendorReadSync()` が要求するまで endpoint をアイドルのままにします。リクエスト／レスポンス型のプロトコルに必要なのはこちらです。Bulk-Only Transport のデバイスはトランザクションの中でしか応答せず、その外でポーリングすると転送エラーになります。モードは open 時に固定され、別のモードで開き直そうとすると失敗します。continuous の転送が `vendorReadSync()` の待つ応答を飲み込んだまま残るより安全なためです。

```cpp
usb.vendorOpen(address, interfaceNumber, ESP_USB_HOST_VENDOR_READ_ON_DEMAND);
usb.vendorWrite(request, sizeof(request), address);
size_t length = 0;
usb.vendorReadSync(response, sizeof(response), &length, 1000, address);
```

`vendorReadSync()` は bulk IN 転送を1つ submit して完了を待つため、`vendorWrite()` と同様に USB callback 内では呼べません。要求長は USB host の要求どおり max packet size の整数倍へ切り上げられ、呼び出し側へは要求した分だけコピーされます。bulk IN と bulk OUT の両方を持つ interface を優先しますが、bulk OUT だけを持つ interface も受け付けます。その場合 IN 転送は開始されず、`vendorRead()` / `onVendorData()` にデータは届きません。たとえばUSBグラフィックスアダプタは bulk OUT と interrupt IN の組み合わせで、このAPIは interrupt IN を使いません。

同じ方向の bulk endpoint が複数ある interface では、descriptor 順で最初の endpoint を選びます。`vendorOutPacketSize()` / `vendorInPacketSize()` は open 済み endpoint の max packet size、`vendorOutEndpoint()` / `vendorInEndpoint()` はそのアドレスを返します（未openは0）。max packet size は転送をパケット境界で終わらせる場合、アドレスはどの endpoint が選ばれたかを確認する場合に使います。

`vendorWrite()` は転送完了を待つため、`onDeviceConnected()` や `onVendorData()` などUSB callback内では呼び出せません。callbackでは送信要求だけを記録し、`loop()` から呼び出してください。`vendorRead()` はノンブロッキングで、deviceごとの512 byte受信バッファから読み出します。`onVendorData()` の `data` ポインタはcallback中だけ有効です。

`vendorControlIn()` は `bmRequestType = 0xc0`、`vendorControlOut()` は `bmRequestType = 0x40` を使います。どちらも vendor 型・device recipient です。`vendorControlTransfer()` は代わりに `bmRequestType` を第 1 引数で受け取ります。vendor 以外のクラスの上に載るプロトコルに必要なもので、たとえば USBTMC は class request を `0xa1` / `0x21`（`wIndex` に interface 番号）で送り、`CLEAR_FEATURE(ENDPOINT_HALT)` のような standard request は `0x02`（`wIndex` に endpoint アドレス）になります。転送方向は `requestType` の bit 7 で決まり、IN 転送では `actualLength` に実受信バイト数が入ります。上記 2 つと同様に完了を待つため USB callback 内では呼べません。EP0 は interface ではなく device に属するので `vendorOpen()` の前でも使えます。これを使ってプロトコルを組んだ例は [`examples/Vendor/EspUsbHostUsbtmcScpi`](examples/Vendor/EspUsbHostUsbtmcScpi/) にあります。

非同期 bulk OUT キューは複数の転送を同時に飛ばし続けるため、転送間でバスがアイドルになりません。

```cpp
bool vendorWriteQueueBegin(size_t depth, size_t bufferBytes,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void vendorWriteQueueEnd(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool vendorWriteQueueReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

uint8_t *vendorWriteAcquire(size_t *capacity, uint32_t timeoutMs = 0,
                            uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool vendorWriteSubmit(uint8_t *buffer, size_t length,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void vendorWriteRelease(uint8_t *buffer, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool vendorWriteAsync(const uint8_t *data, size_t length,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

size_t vendorWritePending(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t vendorWriteQueueFree(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool vendorWriteFlush(uint32_t timeoutMs, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
EspUsbHostVendorWriteStats vendorWriteStats(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
void vendorWriteStatsReset(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

bool vendorWriteZlp(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void vendorSetAutoZlp(bool enable, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool vendorAutoZlp(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
```

`vendorWriteQueueBegin()` は `bufferBytes` サイズの再利用可能な transfer を `depth` 個だけ事前確保します（depthの上限は `ESP_USB_HOST_VENDOR_WRITE_QUEUE_MAX_DEPTH`）。推奨する使い方はゼロコピー経路です。`vendorWriteAcquire()` がプールのDMAバッファを貸し出し、そこへ直接payloadを書き、`vendorWriteSubmit()` で送出します。`vendorWriteAsync()` はコピーする簡易版で、`length` がスロットサイズを超える場合は分割せず失敗します。acquireしたが送らないことにしたスロットは `vendorWriteRelease()` で返します。

いずれも転送完了を待たないため、`vendorWrite()` と違ってUSB callback内からも呼び出せます。完了状況は `vendorWritePending()` / `vendorWriteFlush()` / `vendorWriteStats()` で観測します。`vendorWriteFlush()` は完了callbackが動くtaskそのものであるUSB client taskからは呼び出せません。

ESP32-S3（full-speed OTG）での `tests/manual/vendor_bulk_throughput` 実測値: キューは1.098 MB/sに達し、これはfull-speed bulkの上限1.216 MB/sの約90%です。depth 2あれば転送サイズに関係なくこの上限に張り付きます。同期の `vendorWrite()` が同じ値に届くのは大きな転送のときだけで、転送ごとのレイテンシが支配的になる512 byteでは0.88 MB/sまで落ちます。full-speedではdepthを2より増やしても改善しませんでした。

bulk OUTの転送長がendpointのmax packet sizeの倍数になった場合、その転送だけではUSB転送が終端されません。`vendorSetAutoZlp(true)` にするとライブラリが必要なzero-length packetを付加し、`vendorWriteZlp()` は明示的に1つ送ります。auto ZLPは既定で無効で、有効時はキューのスロットをもう1つ消費するためdepthは2以上にしてください。

### CCIDスマートカードリーダー

CCIDリーダー（`bInterfaceClass == 0x0b`、subclass `0x00`、protocol `0x00`）用のAPIです。

```cpp
bool ccidOpen(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
              uint8_t interfaceNumber = 0xff);
void ccidClose(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool ccidReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool ccidGetInterface(EspUsbHostCcidInterface &info,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint8_t ccidSlotCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

bool ccidGetStatus(EspUsbHostCcidStatus &status, uint8_t slot = 0,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                   uint32_t timeoutMs = 1000);
bool ccidCardPresent(uint8_t slot = 0, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

bool ccidPowerOn(uint8_t *atr = nullptr, size_t atrCapacity = 0,
                 size_t *atrLength = nullptr,
                 EspUsbHostCcidVoltage voltage = ESP_USB_HOST_CCID_VOLTAGE_AUTO,
                 uint8_t slot = 0, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                 uint32_t timeoutMs = ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS);
bool ccidPowerOff(uint8_t slot = 0, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint32_t timeoutMs = 2000);
size_t ccidGetAtr(uint8_t *buffer, size_t capacity, uint8_t slot = 0,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool ccidGetCardInfo(EspUsbHostCcidCardInfo &info, uint8_t slot = 0,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

bool ccidTransfer(const uint8_t *tx, size_t txLength,
                  uint8_t *rx, size_t rxCapacity, size_t *rxLength,
                  uint8_t slot = 0, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint32_t timeoutMs = ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS);
bool ccidApdu(const uint8_t *apdu, size_t apduLength,
              uint8_t *response, size_t responseCapacity, size_t *responseLength,
              uint16_t *statusWord = nullptr,
              uint8_t slot = 0, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
              uint32_t timeoutMs = ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS);
bool ccidEscape(const uint8_t *tx, size_t txLength,
                uint8_t *rx, size_t rxCapacity, size_t *rxLength,
                uint8_t slot = 0, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                uint32_t timeoutMs = ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS);
bool ccidMessage(uint8_t messageType, const uint8_t *messageSpecific,
                 const uint8_t *data, size_t length,
                 EspUsbHostCcidResponse &response,
                 uint8_t slot = 0, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                 uint32_t timeoutMs = ESP_USB_HOST_CCID_DEFAULT_TIMEOUT_MS);

bool ccidAbort(uint8_t slot = 0, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
               uint32_t timeoutMs = 1000);
uint8_t ccidLastError(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
void onCcidCardInserted(CcidSlotChangeCallback callback);
void onCcidCardRemoved(CcidSlotChangeCallback callback);
```

CCID interfaceは列挙時には claim されません。`ccidOpen()` が claim し、CCID class descriptor（slot数、`dwProtocols`、`dwFeatures`、`dwMaxCCIDMessageLength`、exchange level）を読んで `ccidGetInterface()` から取得できるようにし、デバイスごとのメッセージバッファを確保し、リーダーが持っていれば interrupt IN endpoint を開始します。`ccidClose()` はCCIDの動作を停止しバッファを解放しますが、interfaceのclaimは切断まで維持されるため、後から `ccidOpen()` すると再利用されます。

リーダーと通信するAPIはすべて同期APIで、USB callbackから呼んだ場合は `false` を返します。slot変化のcallbackはUSB task上で呼ばれるため、そこでは発生を記録するだけにして、コマンドは `loop()` から発行してください。

`bSeq` はライブラリが管理します。別のコマンドに属するsequence numberの応答は破棄し、command statusが「time extension requested」の応答は最終応答として扱わず待ち続けます。1台のリーダーへのコマンドはデバイスごとのmutexで直列化されます。

`ccidGetCardInfo()` は `ccidPowerOn()` がキャッシュしたATRから、カードの規格（`ISO 14443 A` / `ISO 14443 B` / `ISO 15693` / `FeliCa` / 低周波非接触 / ISO 7816-10メモリカード）とレベル、カード名を取り出します。非接触のストレージカードは自前のATRを持たないため、PC/SC準拠のリーダーがhistorical bytesにPC/SCのRID `A0 00 00 03 06` と標準バイト・カード名を載せた合成ATRを作ります。これを読んでいます。自前のATRを返すカード（接触カードや、ISO 14443-4で話す非接触カード）にはこの識別情報が無いため、`pcscStorageAtr == false` で `ISO 7816 card (own ATR)` として報告します。historical bytesが1バイトも無いATR（リーダーがカードを識別できなかった場合の応答）は推測せず `unknown` のままにします。カード名はPC/SCの代表的な値のみ名前を解決し、`cardName` には常に生のコードが入ります。パーサは `src/EspUsbHostCcidAtr.h` でArduino・USB非依存、host test `tests/unit/ccid_atr` で検証しています。

ATRで判定できないカードは `ccidIdentifyCard()` が Get UID を送り、識別子の形から推定します（8バイトはFeliCaのIDm、ただし先頭が `0xe0` ならISO 15693のUID。7・10バイトはISO 14443 AのNFCID1。4バイトで先頭 `0x08` はISO 14443-3がランダムNFCID1用に予約している値。それ以外の4バイトはNFCID1とPUPIが同じ長さのためtypeを確定しません）。これは識別子の形からの推測であってカード自身の申告ではなく、使われた場合は `info.fromUid` が立ちます。

RC-S300でFeliCaカードを実測した結果: リーダーはPC/SC合成ATR（`PIX.SS = 0x11`、`PIX.Name = 0x003b`）でFeliCaと申告するため、`ccidGetCardInfo()` だけで `FeliCa` と判定でき、Get UIDは8バイトのIDmを返します。同じリーダーにiPhone（Apple Pay）を載せた場合はISO 14443 Aとして応答し、ATRには識別情報が無く、UIDは4バイトのランダムNFCID1になります。これはリーダーの制限ではなくiPhone側の応答です。

`ccidApdu()` は応答からSW1SW2を切り出すため、呼び出し側のバッファはデータ部だけ入れば足ります。`61 xx` と `6C xx` は自動追従せずそのまま返します。`ccidTransfer()` は分離しない同じ送受信で、`ccidEscape()` / `ccidMessage()` はリーダー固有コマンドやこのAPIがラップしていないCCIDメッセージ用です。

チェイン応答（`bChainParameter != 0`、extended APDUレベル）は断片を返さずエラーとして報告します。ICCD変種（interface protocol `0x01` / `0x02`）は未対応です。

メッセージバッファは `ESP_USB_HOST_CCID_BUFFER_SIZE`（512 byte）で、リーダーが報告する `dwMaxCCIDMessageLength` がそれより大きい場合は4096まで拡張します。既定値は `-DESP_USB_HOST_CCID_BUFFER_SIZE=...` で変更できます。

Sony RC-S300（`FeliCa Port/PaSoRi 4.0`）で確認済みです。リーダーはslot 1個・T=1・extended APDU exchange level・`dwMaxCCIDMessageLength = 522` を報告し、上に載せたISO 14443カードは `ISO 14443 A` level 3・`MIFARE Classic 1K` と識別され、PC/SC疑似APDU `FF CA 00 00 00`（Get UID）にUIDと `9000` を返します。FeliCa本来のプロトコルはISO 7816 APDUではないため、FeliCaのブロック読み書き（Read Without Encryptionなど）には `ccidEscape()` によるリーダー固有コマンドが必要です。カード種別とIDmの取得までは標準経路で可能です。

### MIDI

```cpp
void onMidiMessage(MidiCallback callback);   // 受信
EspUsbHostListenerId addMidiMessageListener(MidiCallback callback);

bool midiReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool getMidiPortInfo(EspUsbHostMidiPortInfo &info,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool midiSend(const uint8_t *data, size_t length,
              uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity,
                    uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendControlChange(uint8_t channel, uint8_t control, uint8_t value,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendProgramChange(uint8_t channel, uint8_t program,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendPolyPressure(uint8_t channel, uint8_t note, uint8_t pressure,
                          uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendChannelPressure(uint8_t channel, uint8_t pressure,
                             uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendPitchBend(uint8_t channel, uint16_t value,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendPitchBendSigned(uint8_t channel, int16_t value,
                             uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool midiSendSysEx(const uint8_t *data, size_t length,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
```

`onMidiMessage`コールバックは`const EspUsbHostMidiMessage &message`を受け取ります。フィールド：`cable`、`codeIndex`、`status`、`data1`、`data2`。

`getMidiPortInfo()`は機器が持つcable（仮想MIDIポート）の本数を返します。descriptorから
enumeration時に読むため、受信メッセージの`cable`から事後的に推測するのではなく、
最初のメッセージが届く前に本数が分かります。

```cpp
struct EspUsbHostMidiPortInfo
{
  uint8_t address;
  uint8_t interfaceNumber;
  uint8_t inCableCount;   // 機器 → ホスト
  uint8_t outCableCount;  // ホスト → 機器
};
```

本数はホストから見た方向で、それぞれ自分のbulk endpointに付く
class-specific descriptorから個別に読むため、2つの方向で値が異なることがあります。
cable番号は各方向で`0 .. count - 1`、`EspUsbHostMidiMessage::cable`と同じ基準です。
`midiSend()`は生バイト送信なので、送信パケットのcableは呼び出し側がheaderのnibbleに
立てます。0はその方向のdescriptorが無いか読めなかったことを意味します。

追跡するのは機器の最初のMIDI Streamingインターフェースと、その中の方向ごとに1本の
bulk endpointだけです（送信APIと受信コールバックが使うものと同じ）。cable名
（`iJack`文字列）は読みません。

`addMidiMessageListener()`は[HID入力](#hid入力)のlistenerと同じ契約で受信先を追加し、上限は
`ESP_USB_HOST_MAX_LISTENERS_PER_EVENT`を共有します。1回のbulk転送は複数の4 byteパケットを
運ぶことがあり、登録済みcallbackはパケットごとに順番に呼ばれます。`message.raw`は転送バッファを
指すため、callbackの復帰後も保持したいデータはコピーしてください。

### USBオーディオ

```cpp
void onAudioData(AudioDataCallback callback);
void onAudioOutputRequest(AudioOutputCallback callback);
bool audioInputReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool audioInputStart(uint8_t channels,
                     uint8_t bitsPerSample,
                     uint32_t sampleRate,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioInputStart(const EspUsbHostAudioStreamInfo &stream,
                     uint32_t sampleRate = 0,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioOutputReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool setAudioSampleRate(uint32_t sampleRate,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioOutputStart(uint8_t channels,
                      uint8_t bitsPerSample,
                      uint32_t sampleRate,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioOutputStart(const EspUsbHostAudioStreamInfo &stream,
                      uint32_t sampleRate = 0,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
void audioOutputStop(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioOutputRunning(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint32_t audioOutputUnderruns(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool audioOutputHasFeedback(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint32_t audioOutputFeedbackRate(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint32_t audioOutputFeedbackUpdates(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint32_t audioOutputFeedbackRejects(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
uint32_t audioOutputRate(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool audioSend(const uint8_t *data, size_t length,
               uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
size_t getAudioStreams(uint8_t address, EspUsbHostAudioStreamInfo *streams,
                       size_t maxStreams) const;
size_t getAudioFeatureUnits(uint8_t address,
                            EspUsbHostAudioFeatureUnitInfo *units,
                            size_t maxUnits) const;
bool audioHasMute(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint8_t unitId = 0,
                  uint8_t channel = 0) const;
bool audioHasVolume(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint8_t unitId = 0,
                    uint8_t channel = 0) const;
bool audioGetMute(bool &mute, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint8_t unitId = 0, uint8_t channel = 0);
bool audioSetMute(bool mute, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint8_t unitId = 0, uint8_t channel = 0);
bool audioGetVolume(int16_t &volume, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint8_t unitId = 0, uint8_t channel = 0);
bool audioSetVolume(int16_t volume, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint8_t unitId = 0, uint8_t channel = 0);
bool audioGetVolumeRange(EspUsbHostAudioVolumeRange &range,
                         uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                         uint8_t unitId = 0,
                         uint8_t channel = 0);
bool audioGetVolumeDb(float &db, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint8_t unitId = 0, uint8_t channel = 0);
bool audioSetVolumeDb(float db, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint8_t unitId = 0, uint8_t channel = 0);
bool audioSetVolumeDbClamped(float db, uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                             uint8_t unitId = 0, uint8_t channel = 0);
bool audioConfigureVolume(float db, bool mute = false,
                          uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                          uint8_t unitId = 0, uint8_t channel = 0);
bool audioSetVolumePercent(uint8_t percent,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                           uint8_t unitId = 0, uint8_t channel = 0);
bool audioConfigureVolumePercent(uint8_t percent,
                                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                                 uint8_t unitId = 0, uint8_t channel = 0);
void espUsbHostPrint(const EspUsbHostAudioStreamInfo &stream,
                     Print &out = Serial);
bool espUsbHostAudioStreamSupportsSampleRate(const EspUsbHostAudioStreamInfo &stream,
                                             uint32_t sampleRate);
uint32_t espUsbHostAudioStreamPreferredSampleRate(const EspUsbHostAudioStreamInfo &stream,
                                                  uint32_t preferredSampleRate);
bool espUsbHostAudioStreamMatchesPcm(const EspUsbHostAudioStreamInfo &stream,
                                     uint8_t channels,
                                     uint8_t bytesPerSample,
                                     uint8_t bitsPerSample,
                                     uint32_t sampleRate);
using EspUsbHostAudioStreamFilter = bool (*)(uint32_t sampleRate,
                                             uint8_t channels,
                                             uint8_t bitsPerSample);
EspUsbHostAudioStreamSelection espUsbHostSelectAudioInputStream(
    const EspUsbHostAudioStreamInfo *streams,
    size_t count,
    EspUsbHostAudioStreamFilter filter = nullptr);
EspUsbHostAudioStreamSelection espUsbHostSelectAudioOutputStream(
    const EspUsbHostAudioStreamInfo *streams,
    size_t count,
    EspUsbHostAudioStreamFilter filter = nullptr);
```

`onAudioData`はUSB Audio StreamingのIsochronous INエンドポイントから受信した生ペイロードを通知します。コールバックは`const EspUsbHostAudioData &audio`を受け取り、フィールドは`address`、`interfaceNumber`、`data`、`length`です。

`onAudioOutputRequest`はUSB Audio OUTの推奨APIです。`audioOutputStart()`後、ライブラリがIsochronous OUT転送を駆動し、次のPCMフレームが必要なタイミングでコールバックを呼びます。`request.data`へ最大`request.frameCount`フレームのinterleaved PCMを書き込み、`request.writtenFrames`へ書き込んだフレーム数を設定します。不足分はライブラリが無音として送信し、underrunとしてカウントします。このコールバックはUSB client task上で呼ばれるため、短時間で戻り、ブロックしない処理にしてください。重いデコードはコールバック内で行わず、既存のPCMバッファからコピーする用途に向きます。

非同期playback interfaceはデバイス自身のクロックで動くため、data OUT endpointの隣にexplicit feedback IN endpointを持ち、供給してほしいレートを報告します。ライブラリはplayback中にこのendpointをポーリングし、報告されたレートでOUTパケットを刻みます。1パケットあたりのフレーム数がデバイスに追従するため、ずれていきません。`audioOutputHasFeedback`は動作中のstreamがfeedback endpointを持つかを返し、`audioOutputFeedbackRate`は最後に採用したレート（feedback endpointがなければ`0`）、`audioOutputRate`は実際に刻んでいるレート（feedbackがあればその値、なければネゴシエート済みレート）を返します。ネゴシエート済みレートの±12.5%を外れた値は適用せず`audioOutputFeedbackRejects`にカウントします。バッファが埋まる前に範囲外の値を報告するデバイスはよくあるので、少数のカウントは正常です。増え続ける場合は報告値が使えていません。採用した回数は`audioOutputFeedbackUpdates`です。コールバックが受け取る`request.sampleRate`はネゴシエート済みレートのままです。feedbackが変えるのは1パケットのフレーム数で、フォーマットではありません。

`getAudioStreams`はストリーミングエンドポイントの方向、エンドポイントパケットサイズ、取得できた場合はType Iフォーマット情報を返します。離散サンプルレートまたは連続サンプルレート範囲も取得できます。`protocol`は`ESP_USB_HOST_AUDIO_PROTOCOL_UAC1`または`ESP_USB_HOST_AUDIO_PROTOCOL_UAC2`で、UAC2では`terminalLink`と`clockSourceId`がサンプルレートの取得元Clock Sourceを示します。`startable`は、claimされなかったalt settingが広告しているフォーマットではfalseになります。列挙時に1 interfaceにつき1つのaltしかclaimしないため、16bitと24bit（あるいはサンプルレート）をaltで分けているデバイスでは、残りはフォーマット情報としてのみ報告されます。これらは選択ヘルパーの候補から除外され、`audioInputStart()` / `audioOutputStart()` も拒否します。`espUsbHostSelectAudioInputStream`と`espUsbHostSelectAudioOutputStream`は、任意の`(sampleRate, channels, bitsPerSample)`フィルターを適用したあと、残った候補をスコアリングします。標準スコアでは48 kHz、次に44.1 kHz、16-bit PCM、可能ならstereoを優先します。`audioInputStart(channels, bitsPerSample, sampleRate)`と`audioOutputStart(...)`は、こだわらない引数に`0`を渡せます。`(0, 0, 0)`はデバイスが対応する最良のフォーマットで開始し、`(2, 0, 48000)`は「48 kHz stereo、サンプル幅は任意」を意味します。指定した引数は完全一致が必要で、同条件のaltが複数ある場合はdescriptor順の先頭ではなくスコア最良のものを選びます。`setAudioSampleRate`はサンプリング周波数を設定します（UAC1はエンドポイントへのリクエスト、UAC2はClock Sourceの`SAM_FREQ` control）。`audioSend`はUSB Audio StreamingのIsochronous OUTエンドポイントへ生PCMペイロードを手動送信する低レベルAPIとして残しています。

`getAudioFeatureUnits`は解析済みのAudio Control Feature Unitを返します。`protocol`と`controlSize`がどのレイアウトから読んだかを示します（UAC1は`bControlSize`のstrideで1 control 1ビット、UAC2は4バイト固定strideで1 control 2ビット）。`audioGetMute`、`audioSetMute`、`audioGetVolume`、`audioSetVolume`、dB/range系ヘルパーはFeature Unitのclass-specific requestを使い、デバイスのclass revisionに応じたrequest codeを選びます（UAC1は`GET_CUR` / `GET_MIN` / `GET_MAX` / `GET_RES`、UAC2は`CUR` / `RANGE`）。`audioSetVolumeDbClamped`はrangeが取得できた場合にデバイスのmin/max/resolutionへ丸めます。`audioConfigureVolume`は再生向けの簡易ヘルパーで、対応していればmuteを設定し、volumeをclamp付きdB指定で設定します。percent系ヘルパーは`1..100`をPCM振幅比として扱い、`20 * log10(percent / 100)`でdBへ変換してからmin/maxへclampし、デバイスstepへ丸めます。`0`はmute対応ならmuteし、mute非対応ならminimum volumeへfallbackします。`unitId=0`は指定したcontrolを持つ最初のFeature Unitを選びます。`channel=0`はmaster、`channel=1`以降はチャンネル別controlです。raw volume値はsigned 1/256 dB単位です。

#### Audioの対応範囲

オーディオ対応は **UAC1 (Audio Class 1.0)** と **UAC2 (Audio Class 2.0)** の **Type I PCM** ストリーミングを対象としています。

- **対応:** Isochronous IN/OUTストリーミング、Type Iフォーマット解析とサンプルレート選択、**Feature Unit** の Mute / Volume 制御(get/set、range、dB・percentヘルパー)。UAC2では、interfaceの`bTerminalLink`から辿る **Clock Source** entity（UAC2のdescriptorはサンプルレートを持たないため`SAM_FREQ`の`RANGE`リクエストで取得）、4バイト・2ビットの`bmaControls`レイアウト、1回で済むvolumeの`RANGE`リクエストを含みます。非同期playback interfaceのexplicit feedback endpointはstream一覧には出さず、playback中はポーリングして、デバイスが要求するレートでOUTパケットを刻みます。
- **非対応:** **Clock Selector / Clock Multiplier**、Feature Unit以外のAudio Control Unit(**Mixer / Selector / Processing Unit**)、Mute/Volume以外のFeature Unit control(Bass、Mid、Treble、Automatic Gain、Delay など)。これらがストリーミング開始に必須なデバイスは、列挙はできてもストリーミングできない場合があります。
- **バス速度:** ESP32-S3 / ESP32-S2のホストはfull-speed専用です。UAC2デバイスはhigh-speed前提の設計が多く、full-speed configurationを持たないものはそもそも列挙できません。またfull-speedのisochronousエンドポイントは1フレーム1023バイトが上限です（48 kHz・96 kHzのstereoは収まり、192 kHzのstereoは収まりません）。

UAC1のAudio OUT/IN は標準Arduino `USBAudioCard`、UAC2は `EspUsbDevice` peer（`tests/peer/usb_audio_uac2`）でpeer確認済みですが、実USBマイク・オーディオIFでの確認はまだ限定的です。

### USB Mass Storage

```cpp
bool mscReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool mscInquiry(EspUsbHostMscInquiry &inquiry,
                uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscRequestSense(EspUsbHostMscSense &sense,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscLastSense(EspUsbHostMscSense &sense,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool mscMaxLun(uint8_t &maxLun,
               uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
               uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscSelectLun(uint8_t lun,
                  uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscGetBlockDeviceInfo(EspUsbHostMscBlockDeviceInfo &info,
                           uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                           uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscTestUnitReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscWaitReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                  uint32_t readyTimeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS,
                  uint32_t commandTimeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscCapacity64(uint64_t &blockCount, uint32_t &blockSize,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                   uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscCapacity(uint32_t &blockCount, uint32_t &blockSize,
                 uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                 uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscReadBlocks(uint32_t lba, uint8_t *data, uint32_t blockCount,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                   uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscWriteBlocks(uint32_t lba, const uint8_t *data, uint32_t blockCount,
                    uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                    uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscReadBlocks64(uint64_t lba, uint8_t *data, uint32_t blockCount,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscWriteBlocks64(uint64_t lba, const uint8_t *data, uint32_t blockCount,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscSynchronizeCache(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                         uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscMount(const char *basePath = "/usb",
              uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
              uint8_t lun = 0,
              uint8_t maxFiles = 4,
              uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS,
              bool skipSyncCache = false);
bool mscUnmount(const char *basePath = "/usb");
bool mscMounted(const char *basePath = "/usb") const;

class EspUsbHostMscFS : public fs::FS {
public:
  bool begin(EspUsbHost &host,
             const char *basePath = "/usb",
             uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
             uint8_t lun = 0,
             uint8_t maxFiles = 4,
             uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS,
             bool skipSyncCache = false);
  void end();
  bool mounted() const;
  const char *basePath() const;
  void setSkipSyncCache(bool skip);
  bool skipSyncCache() const;
};
```

MSC対応はSCSI transparent / Bulk-Only TransportのブロックI/O、ESP-IDF FatFs/VFSへのマウント、Arduino `fs::FS` / `File`互換ラッパーに対応しています。

通常のファイル操作では`EspUsbHostMscFS`を使うのが推奨です。`EspUsbHostMscFS`は`fs::FS`を継承しているため、`fs::FS &`を受け取るWebServerやUpdateなどのArduinoライブラリへ渡せます。MSCデバイスがまだ接続されていない、またはメディアが準備できていない間は`EspUsbHostMscFS::begin()`が`false`を返すため、`loop()`内で低頻度に再試行してください。マウント済みかどうかは`mounted()`で確認できます。

`mscReadBlocks64()`、`mscWriteBlocks64()`、`mscInquiry()`、`mscRequestSense()`などの低レベルAPIは、容量表示、ブロックダンプ、診断、独自ファイルシステム実装などで使います。通常のFATファイル操作では、スケッチ側で`mscReady()`、`mscWaitReady()`、`mscGetBlockDeviceInfo()`を直接呼ぶ必要はありません。

ブロックAPIは64-bit LBAに対応しますが、現在のFatFs/VFSマウント経路はESP-IDF側のFatFs設定により32-bit sectorまでです。複数MSCデバイスや複数LUNはAPI上はaddress/LUN指定を持ちますが、ESP32-S3ではHCDチャネル数の制約が強いため、実運用では単一MSCデバイスを前提にしてください。複数MSCはESP32-P4などでの追加検証項目です。

これらのMSC APIはUSB転送完了を待つため、USBコールバック内からは呼ばないでください。書き込み中やファイルを開いたままUSBメモリを抜いた場合、未反映データが失われる可能性があります。抜き差しを扱う場合は、再接続後に再度`begin()`してください。

マウントはFatFsのドライブスロットと登録済みVFSパスを占有し、その数は`CONFIG_FATFS_VOLUME_COUNT`しかないため、USB側と同様に解放が必要です。`EspUsbHost::end()`はこのインスタンスがマウントしたままのボリュームをunmountし、切断時はそのデバイスのボリュームをunmountするので、同じ`basePath`への次の`mscMount()`が「already mounted」で拒否される状態は残りません。この経路でボリュームを外された`EspUsbHostMscFS`は`mounted()`が`false`になり、次の`begin()`で再マウントされます。

一部の非準拠MSCデバイスは、FatFs同期時のSCSI `SYNCHRONIZE CACHE(10)`でSTALLまたは切断することがあります。FatFsの`CTRL_SYNC`経由でも`mscSynchronizeCache()`の直接呼び出しでも、`SYNCHRONIZE CACHE(10)`が失敗するとライブラリはbulk pipeのhaltを解除し、そのデバイスでの失敗を記憶して、以後そのmountおよび再接続までの`mscMount()`でこのコマンドを自動的にスキップします。問題が分かっているデバイスでは、`begin()`前に`usbMassStorage.setSkipSyncCache(true)`を呼ぶか、`begin()` / `mscMount()`へ`skipSyncCache = true`を渡すと最初からスキップできます。互換性は上がりますが、明示的なメディアflushではなく通常のwrite完了に依存します。`mscUnmount()`が発行するflushはbest effortです。拒否するデバイスではログを出して記憶しますが、unmount自体は成功し、`lastError()`も元の値のままにします。どちらにしてもボリュームは切り離されるためです。

### USB Hub

```cpp
bool getHubInfo(uint8_t hubAddress, EspUsbHostHubInfo &hub);
bool getHubPortStatus(uint8_t hubAddress, uint8_t port,
                      uint16_t &status, uint16_t &change);
bool setHubPortPower(uint8_t hubAddress, uint8_t port, bool enable);
```

USB Hubは検出、簡易トポロジー表示、Hub descriptor取得、port status取得、PPPS対応ハブのポート電源ON/OFFに対応しています。`EspUsbHostDeviceInfo::isHub`でHubデバイスかどうかを確認できます。Hub配下のデバイスでは`parentAddress`と`portId`により、どのHub/ポート経由で接続されたかを表示できます。

`getHubInfo()`はHub descriptorを取得し、ポート数、PPPS/ganged/no power switching、over-current方式、power-on-to-power-good時間などを`EspUsbHostHubInfo`へ格納します。`getHubPortStatus()`は各ポートのcurrent statusとchange bitを返します。`setHubPortPower()`はHub class requestでポート電源をON/OFFします。

`setHubTrackingEnabled(false)` にすると、ライブラリは外部ハブに一切触らなくなります。ハブの追跡とはハブをクライアントデバイスとして open してハンドルを保持することで、上記 3 つの呼び出しとハブ自身の connect/disconnect イベントはこれに依存しています。OFF の場合、ハブ配下のデバイスは変わらず列挙・動作しますが、ハブが `getDevices()` に現れず上記の呼び出しは使えません。既定は ON です。

これは挙動の悪いハブに対する解決策ではありません。既知の例として、CH335F（`1a86:8094`）の配下に ALIENTEK DP100 を置くと、追跡を OFF にした状態でも ESP-IDF v5.5.5 自身のハブドライバが（ハブがホストスタックのアドレスリストに載る前に）落ちます。試した組み合わせと `tests/probe/hub_enum` で確認した内容は [tests/manual/README.ja.md](tests/manual/README.ja.md#esp-idf-がクラッシュするハブの組み合わせ) に記録しています。

ポート単位で安全に電源制御するには、HubがPPPS（Per-Port Power Switching）対応として報告される必要があります。ganged powerのHubでは、指定ポートだけでなく複数ポートまたはHub全体に影響する場合があります。USB 3.x Hubや内部で多段Hubになっている製品は挙動が複雑なため、確認用途ではセルフパワーのUSB 2.0 Hubを推奨します。

現状はHub class driverとしての完全な管理ではなく、利用者向けの情報取得と明示的なポート電源制御を提供する段階です。port change bitのclear、複数段Hub、USB 3.x Hub互換性、ESP32-P4のFS/HS差分は継続確認項目です。これらのAPIはUSB転送完了を待つため、USBコールバック内からは呼ばないでください。

### USBネットワーク（CDC-NCM / CDC-ECM）

```cpp
size_t getNetworkInterfaces(uint8_t address,
                            EspUsbHostNetworkInterfaceInfo *interfaces,
                            size_t maxInterfaces);
bool networkOpen(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool networkOpen(const EspUsbHostNetworkInterfaceInfo &network);
void networkClose(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool networkReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool networkLinkUp(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;

// 生Ethernetフレーム
void onNetworkFrame(NetworkFrameCallback callback);
bool   networkWriteFrame(const uint8_t *frame, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
size_t networkReadFrame(uint8_t *buffer, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS);

// lwIP（esp_netif）統合
bool      networkAttachNetif(const EspUsbHostNetworkConfig &config = EspUsbHostNetworkConfig(),
                             uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool      networkDetachNetif(uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
IPAddress networkLocalIP(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool      networkStats(EspUsbHostNetworkStats &stats, uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
```

`networkAttachNetif()`はCDC-NCM/ECM interfaceを（未openなら）openし、`esp_netif`の
インターフェースとして登録します。これにより標準のArduinoネットワーク（`NetworkClient`、
`HTTPClient`）がUSB NIC経由で動作します。`EspUsbHostNetworkConfig`は既定でDHCPクライアントで、
固定アドレスにするなら`dhcpClient=false`にして`ip`/`gateway`/`subnet`（`/dns1`/`dns2`）を
設定します。IPスタックを使わず生Ethernetフレームを扱う場合は`onNetworkFrame()` /
`networkWriteFrame()` / `networkReadFrame()`を使い、netifはattachしません。deviceが両方に
対応している場合はCDC-NCMをCDC-ECMより優先します。

`getNetworkInterfaces()`は**全**configurationを走査（`usb_host_get_config_desc()`）して候補を
`configurationValue`付きで返しますが、`networkOpen()`は**active**なconfiguration内の候補しか
受け付けません。したがって、network機能が既定configurationに無いアダプタでは
`setConfigurationSelector()`と2パスの列挙が必要です。詳細と2パス目の自動化方法は
[examples/UsbNetwork/](examples/UsbNetwork/)を参照してください。

lwIP統合にはビルドに`esp_netif`が必要です（標準のArduino-ESP32 coreには含まれます）。無い場合
`networkAttachNetif()`は`false`を返し、生フレームAPIは引き続き使えます。これらのAPIはUSB
コールバック内ではなくapplication taskから呼び出してください。

**NTBサイズのネゴシエーション（CDC-NCM）。** NCM deviceはEthernet datagramをNTBにまとめて送り、
その大きさは申告した`dwNtbInMaxSize`までdevice自身が決めます（hostが下げない限り）。
複数datagramのまとめ送りはトラフィックが増えてから始まるため、「1フレーム分あれば足りる」
サイズの受信バッファは軽い動作確認では問題なく通り、負荷が上がるとNTBを丸ごと取りこぼします。
リンクは正常に見えたまま激しいパケットロスとして現れるので厄介です。そこでopen時に
`GET_NTB_PARAMETERS`を読み、`bmNetworkCapabilities`のbit 3が対応を示していれば
`SET_NTB_INPUT_SIZE`でdevice側を制限し、そうでなければdeviceの最大値に合わせて自分の
バッファを確保します（上限は`ESP_USB_HOST_NETWORK_NTB_IN_LIMIT`＝16KB、`-D`で変更可）。
決定値は`EspUsbHostNetworkStats::ntbInSize`で確認できます。ESP-IDFはIN転送長をMPSの整数倍と
規定しているため、この値は常にbulk IN endpointのmax packet sizeの倍数になります
（High-Speedでは3200ではなく3072になるのはこのため）。`rxOversized`はサイズ超過で破棄した
NTB数で、deviceが申告以上のNTBを送らない限り0のままです。`linkUp`と`txFails`が正常なのに
スループットが出ないときに最初に見るべき値です。

### デバイス探索

```cpp
size_t deviceCount() const;
size_t getDevices(EspUsbHostDeviceInfo *devices, size_t maxDevices) const;
bool   getDevice(uint8_t address, EspUsbHostDeviceInfo &device) const;
size_t getInterfaces(uint8_t address, EspUsbHostInterfaceInfo *interfaces,
                     size_t maxInterfaces) const;
size_t getEndpoints(uint8_t address, EspUsbHostEndpointInfo *endpoints,
                    size_t maxEndpoints) const;
size_t endpointChannelCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t managedEndpointCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t ep0ChannelCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t hubEndpointChannelCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t estimatedHcdChannelCount(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
size_t maxEndpointChannelCount() const;
void   espUsbHostPrint(const EspUsbHostInterfaceInfo &interface,
                       Print &out = Serial);
void   espUsbHostPrint(const EspUsbHostEndpointInfo &endpoint,
                       Print &out = Serial);
void   printDeviceInfo(uint8_t address, bool includeHubInfo = false,
                       Print &out = Serial);
void   printAllDeviceInfo(Print &out = Serial);
```

配列サイズ定数：`ESP_USB_HOST_MAX_DEVICES`、`ESP_USB_HOST_MAX_INTERFACES`、`ESP_USB_HOST_MAX_ENDPOINTS`。
`endpointChannelCount()`はclaimに成功したinterface内のendpoint数です。`managedEndpointCount()`は、このライブラリが継続受信用transferを持って管理しているendpoint数です。`estimatedHcdChannelCount()`は実験用の推定値で、追跡中デバイスをEP0/control pipeとして数え、claim済みendpoint数とHub descriptor上のendpoint数を加算します。

### エラーハンドリング

```cpp
int         lastError() const;
const char *lastErrorName() const;
```

## 設計方針

**コールバック登録スタイル。** `onKeyboard()`・`onMouse()` などにラムダや関数を登録して使います。`EspUsbHost` を継承して仮想関数をオーバーライドする旧スタイルは主要APIではありません。

**2系では破壊的変更を許容。** 旧来の継承ベースのAPIとの後方互換性よりも、クリーンなArduino向けAPIを優先します。2系の間でも、サンプルや実機確認からよりよい形が見えた場合はAPIを変更する可能性があります。

**HIDゲームパッドレポートはマッピング前提のデータとして公開します。** `onGamepad()`はディスクリプタからデコードしたフィールドとraw/reportバイトを公開します。左スティックX/Y・右スティックX/Yのような意味名は、デバイスごとに位置が異なり、8bit・12bit・16bit・bit詰めなど幅も異なるため割り当てません。利用側で`vid` / `pid`、`fields`、`rawData`、`reportData`を見て、そのコントローラーに合うマッピングを作ってください。

**非ゴール：**
- 初期段階からすべてのHIDレポートディスクリプタを完全自動解釈すること
- 初回リリースですべてのUSBクラスを実装すること
- ESP-IDF HID Host Driver APIとの互換性
- USBスペックの内部仕様をそのままArduino APIとして公開すること

## 複数デバイスの扱い

複数デバイスを使う場合は、セルフパワーのUSB 2.0ハブを推奨します。安価なバスパワーのハブでは、デバイスを認識しなかったり、消費電流によって動作が不安定になることがあります。USB 3.xハブも内部トポロジーや挙動が複雑なものが多く、このライブラリでの確認用としては最初に選ぶ対象ではありません。

ポート数は4ポートまでのハブがおすすめです。ポート数が多いハブは内部でハブが多段構成になっていることがあり、トポロジーが深くなり、必要なリソースも増えます。ESP32-S3のUSBホストチャネル数は8個しかなく、1つのUSBデバイスがインターフェースやエンドポイントのために複数チャンネルを使うこともあるため、複数デバイス構成では上限に当たりやすくなります。

送信APIと`EspUsbHostCdcSerial`はデフォルトで`ESP_USB_HOST_ANY_ADDRESS`を使用し、該当クラスの最初のデバイスを対象にします。特定の接続中デバイスを指定する場合は、明示的に`address`を渡します。

デバイス識別に使えるフィールドは、それぞれ安定性が違います：

| フィールド | 用途 |
|------------|------|
| `address` | 現在のUSBアドレス。`setAddress()`、`sendSerial()`、`midiSend()`、`setKeyboardLeds()`などのAPI呼び出しに使います。抜き差し後に変わることがあります。 |
| `portId` | 物理/トポロジ上の接続位置。同じポートに再接続されたデバイスを追跡する用途に向きます。 |
| `vid` / `pid` | デバイスの種類・モデル識別。対応チップや製品の判定に使います。 |
| `manufacturer` / `product` | USB文字列ディスクリプタの表示名。ログやUI向きですが、一意とは限りません。 |
| `serial` | デバイスが提供するUSBシリアル番号文字列。取得できる場合は個体識別に使えますが、空文字の場合があります。 |

```cpp
usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
  if (device.vid == 0x0403) {
    CdcSerial.setAddress(device.address);
  }
});
```

## テスト

- [`tests/peer/`](tests/peer/) — ESP32-S3をデバイス側として使う2台構成のUSBテスト。ピア側は主にArduino-ESP32標準USB Device実装を使い、Hostの基本相互運用性を確認
- [`tests/loopback/`](tests/loopback/) — 1台でのループバックテスト。ESP32-P4向けの実用的な整備は兄弟ライブラリ`EspUsbDevice`側で進行中
- [`tests/manual/`](tests/manual/) — 特殊ハードウェアや人による確認が必要な手動テスト

セットアップ方法は[tests/README.ja.md](tests/README.ja.md)を参照してください。

手動実行の _Library Footprint Matrix_ workflowでは、代表exampleの`sketch.yaml`に指定されたArduinoコアへ固定し、Base、HID、Serial、Audio、Storage、MIDI、Vendor、Network、Infoの固定probeを、指定した各ライブラリリリースに対してビルドします。正規化したFlash/静的RAMは常に1組のJSONとMarkdownレポートへ上書きし、コンパイラログ、ELF、map、アプリケーションbinは保存期間を限定したworkflow artifactとして残します。Arduinoコアを切り替えてビルド互換性を調べるcore compatibility matrixとは別の用途です。

## リリースチェックリスト

1. **作業ツリーのクリーン確認** — `git status` で未コミットの変更がないことを確認する
2. **依存バージョンの確認・更新** — [vscode-arduino-cli-wrapper](https://marketplace.visualstudio.com/items?itemName=tanakamasayuki.vscode-arduino-cli-wrapper) の _sketch.yaml Versions_ 機能で全 `sketch.yaml` のボード・ライブラリバージョンを確認し、更新があれば最新にしてから手順 3〜5 をやり直す
3. **ビルドチェック** — vscode-arduino-cli-wrapper の _Build Check_ を使用するか、`python tools/build_check.py <プロファイル>` で該当 sketch.yaml プロファイルを持つ全 example をビルドする（例: `python tools/build_check.py esp32s2`）。最低限 `examples/` の `esp32s3` プロファイル。ESP32-P4 関連の変更がある場合は全プロファイルも確認する。静的RAM使用量が増える変更では `esp32s2` プロファイルもビルドすること（S2は内蔵RAMが少なく `ESP_USB_HOST_MAX_DEVICES` の既定値も小さい）。`dram0_0_seg overflowed` の再発を早期に検出できる。`UsbNetwork` 例はS2マトリクスから意図的に除外している。
4. **自動テスト** — `peer/` または `loopback/` のテストがすべて通っていること
5. **手動テスト** — 改修内容に関連するテストを実行する（`tests/.pytest-results/state.json` で最終実行日時を確認）。必須ではないが強く推奨
6. **CHANGELOG** — 今回のリリースのエントリが正確で漏れがないか確認・更新する
7. **ドキュメント** — API リファレンス・サンプル・README が変更内容を反映していることを確認する
8. **リリース** — GitHub Actions のリリースワークフローを実行する（`workflow_dispatch`、デフォルトは patch バンプ）。ワークフローで行われること：
   - `library.properties` のバージョンを更新（major / minor / patch を選択可能）
   - バージョン更新をデフォルトブランチにコミット・プッシュ
   - `tests/` を除いた `release` ブランチを作成
   - ライブラリの `.zip` アーカイブをビルド
   - `CHANGELOG.md` からリリースノートを抽出
   - アーカイブとリリースノートを添付した GitHub Release を作成

## ライセンス

MIT License です。[LICENSE](LICENSE) を参照してください。
