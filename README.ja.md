# EspUsbHost

> English: [README.md](README.md)

ESP32-S3 / ESP32-P4でUSB Hostを使うためのArduinoライブラリです。

USB処理はバックグラウンドのFreeRTOSタスクで行われるため、`loop()`でUSBポーリング関数を呼ぶ必要はありません。`setup()`でコールバックを登録して`begin()`を呼ぶだけで動作します。

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
- **デバイス探索** — 接続デバイス・インターフェース・エンドポイントの列挙
- **複数デバイス対応** — 各コールバックと送信APIにオプションの`address`引数があり、特定デバイスを指定可能

## ロードマップ

### USBクラス対応状況

| クラス | 状況 |
|--------|------|
| HID — キーボード・マウス・ゲームパッド・コンシューマーコントロール・システムコントロール・ベンダー | ✅ 実装済み |
| USBシリアル — CDC ACM・VCP（FTDI・CP210x・CH34x）を`EspUsbHostCdcSerial`で統一対応。baud、データビット、パリティ、ストップビットを設定可能 | ✅ 実装済み |
| USB MIDI | ✅ 実装済み |
| UAC — USBオーディオ入出力 | 🔲 実験的。Audio OUTはpeer確認済み、Audio INはAPIあり・実データ確認が残っています |
| HUB — ハブ検出・トポロジー情報・ポート電源制御 | ✅ 基本実装済み。`hub_info`と`hub_power`のmanual確認済み。change bit処理、複数段Hub、USB 3.x Hub互換性は継続確認 |
| MSC — USBストレージのブロックI/OとFatFs/Arduino FSマウント | ✅ 基本実装済み。単一MSCデバイスでpeer/manual確認済み。非準拠デバイス向けの`SYNCHRONIZE CACHE(10)`フォールバックあり。複数MSC・複数LUN・異常系BOT完全復旧は後回し |
| UVC — USBカメラ | 💭 検討中 |

### その他の予定機能

| 機能 | 状況 |
|------|------|
| `onHIDReportDescriptor()` — HIDレポートディスクリプタの取得 | ✅ 実装済み |
| HIDゲームパッド入力 — ユーザー定義マッピング用のディスクリプタデコード済みフィールドとraw/reportバイト | ✅ マッピング前提のイベントAPI。マッピング補助は検討中 |
| チャンネル数・endpoint使用量の可視化 | ✅ 実験的な診断APIとして実装済み。複数デバイスやAudio/MSC/HUB併用時の上限把握に使う |
| USB Audio INの実データ確認 | 🔲 次候補。実USBマイクまたはpeer側Audio IN生成の安定化が必要 |
| ESP32-P4検証 | 🔲 継続。FS/HS OTG、HUB可否、ループバックテストを個別確認する |
| ループバックテスト（ESP32-P4 1台構成） | 🔲 `EspUsbDevice` 側で整備中。このリポジトリ側の `tests/loopback` はREADMEのみ |
| 手動テスト — VCPシリアル・複数デバイス・活線挿抜 | ✅ 主要ケース確認済み。追加デバイス互換性は継続 |

## 現状の制限と注意点

- **バージョン互換性:** 2系は1系とソース互換ではありません。1系のスケッチは、コールバック登録APIと新しいクラス別APIへ移行してください。
- **今後の破壊的変更:** 実機やサンプルからよりよいAPI形状が見えた場合、2系の間でも互換性を壊す変更が入る可能性があります。
- **USB Hostリソース:** ESP32-S3のUSB host channel数は少ないです。複合デバイス、Hub、Audio、MSC、複数シリアルデバイスではすぐ上限に近づきます。`printDeviceInfo()` / `printAllDeviceInfo()` やendpoint/channel診断APIで使用量を確認してください。
- **USB Hub:** 複数デバイスの確認にはセルフパワーのUSB 2.0 Hubを推奨します。USB 3.x Hubや内部多段Hubは挙動が複雑で、十分に検証できていません。
- **USB Audio:** 出力の方が入力より確認が進んでいます。Audio INのペイロード受信APIはありますが、実USBマイクなどでの確認はまだ限定的です。UAC2のClock/Selectorや高度なAudio Control Unit対応も限定的です。
- **Mass Storage:** FAT利用は実用上単一MSCデバイスを前提にしてください。複数MSC、複数LUN、特殊なblock size、異常系BOT復旧は追加検証が必要です。非準拠デバイスではMSC節の`SYNCHRONIZE CACHE(10)`フォールバックが必要になる場合があります。
- **活線挿抜:** ファイル、シリアル転送、Audio stream、各クラス操作の途中で抜くと、デバイスによっては失敗やデータ喪失が起こります。
- **ESP32-P4:** `EspUsbHostConfig::port`でFS/HS OTGを選べますが、P4向け検証は継続中です。特にHS OTGとHubの組み合わせには実用上の制限があります。

## 必要環境

- ESP32-S3、またはArduino-ESP32 USB Hostに対応したボード
- Arduino-ESP32コア

### ESP32-P4 の注意事項

ESP32-P4搭載ボードでは、ボード設計によって最大4系統のUSB関連ポート/経路が見えることがあります：

- 書き込み・ログ用の外付けUSB-UARTシリアル変換（CH34xなど）
- USB FSのCDC
- USB FSのOTG
- USB HSのOTG

ボードによっては、外付けUSB-UARTシリアル変換の先に通常のUARTポートがあります。これはESP32-P4内蔵のUSB Serial/JTAGやUSB OTG peripheralとは別の経路です。

SoCレベルの信号ピンは以下です。実際のボードでどのコネクタに配線されているかはボード設計によって異なるため、配線やポート選択の前に回路図を確認してください。

| ESP32-P4での典型的な役割 | D- | D+ | 備考 |
|--------------------------|----|----|------|
| USB CDC FS / USB Serial/JTAG FS | GPIO24 | GPIO25 | 内蔵USB Serial/JTAG、またはFSデバイス側CDCのコネクタとして使われることが多いペアです。ボードによってはUSB Host用コネクタと混同しやすいです。 |
| USB OTG FS | GPIO26 | GPIO27 | Full-speed OTGコネクタとして使われることが多いペアです。USB Hostでは`ESP_USB_HOST_PORT_FULL_SPEED`で選択します。 |
| USB OTG HS | package pin 49 | package pin 50 | High-speed OTGポート。汎用GPIOではなくUSB専用ピンです。`ESP_USB_HOST_PORT_HIGH_SPEED`で選択します。 |

ESP32-P4にはFull-speed USB PHYのピンペアが2つあり、FS OTGとUSB Serial/JTAGはGPIO24/GPIO25またはGPIO26/GPIO27から選択できます。上の表は典型的/デフォルトの割り当てであり、すべてのボードで同じとは限りません。

FS USBのピンペア選択はeFuseで変更できますが、一度スワップすると不可逆で元に戻せません。また、通常のライブラリ利用では変更は推奨されません。基本的にはボードのデフォルト配線を使い、USB Hostで使うperipheralは`EspUsbHostConfig::port`で選択してください。

USB Hostとして使えるのはOTGポートです。ボードによっては、どのコネクタがFS OTGで、どれがCDC/デバイス用なのか分かりにくい場合があります。ボードの回路図とサンプル設定を確認してください。

ESP-IDFのUSB Hostスタックは、同時に1つのHost peripheralしか利用できません。ESP32-P4では、`EspUsbHostConfig::port`でFS OTGまたはHS OTGのどちらかを選択します。FS OTGとHS OTGを同時にUSB Hostとして使うことはできません。Arduinoライブラリとして使う場合、USBデバイス機能はHS側を使用し、FS側をデバイス機能として選択することはできません。

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
| [EspUsbHostMouse](examples/HID/EspUsbHostMouse/) | マウスの移動量とボタン操作を取得 |
| [EspUsbHostCompositeHID](examples/HID/EspUsbHostCompositeHID/) | キーボード+マウスなどの複合HIDデバイスを扱う |
| [EspUsbHostConsumerControl](examples/HID/EspUsbHostConsumerControl/) | メディアキー（音量・再生/一時停止など）を検出 |
| [EspUsbHostSystemControl](examples/HID/EspUsbHostSystemControl/) | システムキー（電源・スタンバイ・ウェイクアップ）を検出 |
| [EspUsbHostGamepad](examples/HID/EspUsbHostGamepad/) | ゲームパッドのスティック・十字キー・ボタンを取得 |
| [EspUsbHostHIDVendor](examples/HID/EspUsbHostHIDVendor/) | ベンダーHID入力と出力/フィーチャーレポートの送信 |
| [EspUsbHostHIDRawDump](examples/HID/EspUsbHostHIDRawDump/) | デバイスアドレス付きでHexダンプ（複数デバイス対応） |

### Info

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostDeviceInfo](examples/Info/EspUsbHostDeviceInfo/) | 接続中の全デバイスのディスクリプタ・インターフェース・エンドポイントを表示 |
| [EspUsbHostHIDReportDescriptor](examples/Info/EspUsbHostHIDReportDescriptor/) | HID調査用にHIDレポートディスクリプタと簡易item decodeを表示 |
| [EspUsbHostCustomDeviceCallbacks](examples/Info/EspUsbHostCustomDeviceCallbacks/) | 接続・切断コールバックを自分で定義し、接続デバイスを調べる |
| [EspUsbHostHubPPPS](examples/Info/EspUsbHostHubPPPS/) | PPPS対応USBハブのポート電源を制御 |

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

### Storage

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostMSCBlockDump](examples/Storage/EspUsbHostMSCBlockDump/) | MSCの容量情報を表示し、先頭ブロックをダンプ |
| [EspUsbHostMSCFatList](examples/Storage/EspUsbHostMSCFatList/) | MSCをArduino `fs::FS`としてマウントし、ファイル一覧と小さなwrite/read/delete確認を行う |

## APIリファレンス

### コア

```cpp
bool begin();
bool begin(const EspUsbHostConfig &config);
void end();
bool ready() const;
```

`EspUsbHostConfig`でバックグラウンドタスクのスタックサイズ・優先度・コアを調整できます：

```cpp
struct EspUsbHostConfig {
  uint32_t    taskStackSize = 4096;
  UBaseType_t taskPriority  = 5;
  BaseType_t  taskCore      = tskNO_AFFINITY;
  EspUsbHostPort port       = ESP_USB_HOST_PORT_DEFAULT;
};
```

ESP32-P4で特定のOTG peripheralを選びたい場合は、`port`に`ESP_USB_HOST_PORT_FULL_SPEED`または`ESP_USB_HOST_PORT_HIGH_SPEED`を指定します。他のチップではこの設定は無視されます。

### デバイスイベント

```cpp
void onDeviceConnected(DeviceCallback callback);
void onDeviceDisconnected(DeviceCallback callback);
void espUsbHostPrint(const EspUsbHostDeviceInfo &device, Print &out = Serial);
```

コールバックは`const EspUsbHostDeviceInfo &device`を受け取ります。主要フィールド：`address`、`vid`、`pid`、`product`、`manufacturer`、`serial`、`speed`、`parentAddress`、`portId`。
`espUsbHostPrint(device)`は1行サマリを出力します。`connected:`や`disconnected:`などのイベント文脈はコールバック側で付けてください。

`portId`はデバイスの接続位置を表します。`0x01`はルートポート直結です。ハブ配下のデバイスでは上位ニブルが検出順に割り当てられたハブ番号、下位ニブルがそのハブのポート番号です。例えば`0x12`は「ハブ#1のポート2」を表します。

### HID入力

```cpp
void onKeyboard(KeyboardCallback callback);
void onMouse(MouseCallback callback);
void onConsumerControl(ConsumerControlCallback callback);
void onSystemControl(SystemControlCallback callback);
void onGamepad(GamepadCallback callback);
void onHIDInput(HIDInputCallback callback);    // 生データ — 全HIDインターフェースで発火
void onVendorInput(VendorInputCallback callback);
void espUsbHostPrint(const EspUsbHostHIDInput &input, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostKeyboardEvent &event, Print &out = Serial);
const char *espUsbHostConsumerControlUsageName(uint16_t usage);
const char *espUsbHostSystemControlUsageName(uint8_t usage);
```

主なイベントフィールド：

パース済みHIDコールバック（`onKeyboard`、`onMouse`、`onConsumerControl`、`onSystemControl`、`onGamepad`、`onVendorInput`）はすべて、`vid`、`pid`、`manufacturer`、`product`、`serial`、入力レポート全体を指す`rawData` / `rawLength`、Report IDがある場合にそれを除いたレポートバイトを指す`reportData` / `reportLength`を含みます。

| コールバック | 主要フィールド |
|-------------|--------------|
| `onKeyboard` | `pressed`、`keycode`、`ascii`、`modifiers`、`address` |
| `onMouse` | `x`、`y`、`wheel`、`buttons`、`previousButtons`、`moved`、`buttonsChanged`、`address` |
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
bool sendHIDReport(uint8_t interfaceNumber, uint8_t reportType, uint8_t reportId,
                   const uint8_t *data, size_t length,
                   uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool sendVendorOutput(const uint8_t *data, size_t length,
                      uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool sendVendorFeature(const uint8_t *data, size_t length,
                       uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
```

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
```

`EspUsbHostCdcSerial`は標準Arduino `Stream`/`Print`ラッパーです：

```cpp
EspUsbHostCdcSerial CdcSerial(usb);

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

`EspUsbHostSerialConfig`のデフォルトは115200 8N1です。`dataBits`は5〜8ビット、`parity`は`ESP_USB_HOST_SERIAL_PARITY_NONE`、`ODD`、`EVEN`、`MARK`、`SPACE`、`stopBits`は`ESP_USB_HOST_SERIAL_STOP_BITS_1`、`1_5`、`2`を指定できます。

複数のUSBシリアルデバイスが接続されている場合は、`onDeviceConnected`内で`setAddress()`を呼び特定デバイスにバインドします。

### MIDI

```cpp
void onMidiMessage(MidiCallback callback);   // 受信

bool midiReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
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

`getAudioStreams`はストリーミングエンドポイントの方向、エンドポイントパケットサイズ、取得できた場合はUAC1 Type Iフォーマット情報を返します。離散サンプルレートまたは連続サンプルレート範囲も取得できます。`espUsbHostSelectAudioInputStream`と`espUsbHostSelectAudioOutputStream`は、任意の`(sampleRate, channels, bitsPerSample)`フィルターを適用したあと、残った候補をスコアリングします。標準スコアでは48 kHz、次に44.1 kHz、16-bit PCM、可能ならstereoを優先します。`setAudioSampleRate`はAudio Streamingエンドポイントを有効化するときに送るUAC1 sampling frequencyリクエストの値を設定します。`audioSend`はUSB Audio StreamingのIsochronous OUTエンドポイントへ生PCMペイロードを手動送信する低レベルAPIとして残しています。

`getAudioFeatureUnits`は解析済みのUAC1 Audio Control Feature Unitを返します。`audioGetMute`、`audioSetMute`、`audioGetVolume`、`audioSetVolume`、dB/range系ヘルパーはUAC1のFeature Unit class-specific requestを使います。`audioSetVolumeDbClamped`はrangeが取得できた場合にデバイスのmin/max/resolutionへ丸めます。`audioConfigureVolume`は再生向けの簡易ヘルパーで、対応していればmuteを設定し、volumeをclamp付きdB指定で設定します。percent系ヘルパーは`1..100`をPCM振幅比として扱い、`20 * log10(percent / 100)`でdBへ変換してからmin/maxへclampし、デバイスstepへ丸めます。`0`はmute対応ならmuteし、mute非対応ならminimum volumeへfallbackします。`unitId=0`は指定したcontrolを持つ最初のFeature Unitを選びます。`channel=0`はmaster、`channel=1`以降はチャンネル別controlです。raw volume値はsigned 1/256 dB単位です。

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

一部の非準拠MSCデバイスは、FatFs同期時のSCSI `SYNCHRONIZE CACHE(10)`でSTALLまたは切断することがあります。FatFsの`CTRL_SYNC`中にこのコマンドが失敗した場合、そのmountでは以後このコマンドを自動的にスキップします。問題が分かっているデバイスでは、`begin()`前に`usbMassStorage.setSkipSyncCache(true)`を呼ぶか、`begin()` / `mscMount()`へ`skipSyncCache = true`を渡すと最初からスキップできます。互換性は上がりますが、明示的なメディアflushではなく通常のwrite完了に依存します。

### USB Hub

```cpp
bool getHubInfo(uint8_t hubAddress, EspUsbHostHubInfo &hub);
bool getHubPortStatus(uint8_t hubAddress, uint8_t port,
                      uint16_t &status, uint16_t &change);
bool setHubPortPower(uint8_t hubAddress, uint8_t port, bool enable);
```

USB Hubは検出、簡易トポロジー表示、Hub descriptor取得、port status取得、PPPS対応ハブのポート電源ON/OFFに対応しています。`EspUsbHostDeviceInfo::isHub`でHubデバイスかどうかを確認できます。Hub配下のデバイスでは`parentAddress`と`portId`により、どのHub/ポート経由で接続されたかを表示できます。

`getHubInfo()`はHub descriptorを取得し、ポート数、PPPS/ganged/no power switching、over-current方式、power-on-to-power-good時間などを`EspUsbHostHubInfo`へ格納します。`getHubPortStatus()`は各ポートのcurrent statusとchange bitを返します。`setHubPortPower()`はHub class requestでポート電源をON/OFFします。

ポート単位で安全に電源制御するには、HubがPPPS（Per-Port Power Switching）対応として報告される必要があります。ganged powerのHubでは、指定ポートだけでなく複数ポートまたはHub全体に影響する場合があります。USB 3.x Hubや内部で多段Hubになっている製品は挙動が複雑なため、確認用途ではセルフパワーのUSB 2.0 Hubを推奨します。

現状はHub class driverとしての完全な管理ではなく、利用者向けの情報取得と明示的なポート電源制御を提供する段階です。port change bitのclear、複数段Hub、USB 3.x Hub互換性、ESP32-P4のFS/HS差分は継続確認項目です。これらのAPIはUSB転送完了を待つため、USBコールバック内からは呼ばないでください。

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

セットアップ方法は[tests/README.md](tests/README.md)を参照してください。

## リリースチェックリスト

1. **作業ツリーのクリーン確認** — `git status` で未コミットの変更がないことを確認する
2. **依存バージョンの確認・更新** — [vscode-arduino-cli-wrapper](https://marketplace.visualstudio.com/items?itemName=tanakamasayuki.vscode-arduino-cli-wrapper) の _sketch.yaml Versions_ 機能で全 `sketch.yaml` のボード・ライブラリバージョンを確認し、更新があれば最新にしてから手順 3〜5 をやり直す
3. **ビルドチェック** — vscode-arduino-cli-wrapper の _Build Check_ を使用。最低限 `examples/` の `esp32s3` プロファイル。ESP32-P4 関連の変更がある場合は全プロファイルも確認する
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
