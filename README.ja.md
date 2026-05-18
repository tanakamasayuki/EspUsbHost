# EspUsbHost

> English: [README.md](README.md)

ESP32-S3でUSB Hostを使うためのArduinoライブラリです。

USB処理はバックグラウンドのFreeRTOSタスクで行われるため、`loop()`でUSBポーリング関数を呼ぶ必要はありません。`setup()`でコールバックを登録して`begin()`を呼ぶだけで動作します。

## 機能

- **HID入力** — キーボード・マウス・コンシューマーコントロール（メディアキー）・システムコントロール（電源/スタンバイ）・ゲームパッド
- **HID出力** — キーボードLED制御・ベンダー出力/フィーチャーレポート
- **USBシリアル** — CDC ACMおよび主要VCPデバイス（FTDI・CP210x・CH34x）を`EspUsbHostCdcSerial`で統一対応（Arduino `Stream`/`Print` 互換）
- **MIDI** — USB MIDI入出力
- **USBオーディオ** — USB Audio StreamingインターフェースのIsochronous INペイロード受信とIsochronous OUT送信
- **デバイス探索** — 接続デバイス・インターフェース・エンドポイントの列挙
- **複数デバイス対応** — 各コールバックと送信APIにオプションの`address`引数があり、特定デバイスを指定可能

## ロードマップ

### USBクラス対応状況

| クラス | 状況 |
|--------|------|
| HID — キーボード・マウス・ゲームパッド・コンシューマーコントロール・システムコントロール・ベンダー | ✅ 実装済み |
| USBシリアル — CDC ACM・VCP（FTDI・CP210x・CH34x）を`EspUsbHostCdcSerial`で統一対応。データビットは8ビットのみで、シリアル形式設定は未実装 | ✅ 実装済み |
| USB MIDI | ✅ 実装済み |
| UAC — USBオーディオ入出力 | 🔲 実験的 |
| HUB — ハブ検出・トポロジー情報・ポート電源制御 | 🔲 部分実装。手動テストの`hub_info`と`hub_power`はPASS |
| MSC — USBストレージ | 💭 検討中 |
| UVC — USBカメラ | 💭 検討中 |

### その他の予定機能

| 機能 | 状況 |
|------|------|
| `onHIDReportDescriptor()` — HIDレポートディスクリプタの取得 | 🔲 実装予定 |
| ループバックテスト（ESP32-P4 1台構成） | 🔲 整備中 |
| 手動テスト — VCPシリアル・複数デバイス・活線挿抜 | 🔲 整備中 |

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
| [EspUsbHostConsumerControl](examples/HID/EspUsbHostConsumerControl/) | メディアキー（音量・再生/一時停止など）を検出 |
| [EspUsbHostSystemControl](examples/HID/EspUsbHostSystemControl/) | システムキー（電源・スタンバイ・ウェイクアップ）を検出 |
| [EspUsbHostGamepad](examples/HID/EspUsbHostGamepad/) | ゲームパッドのスティック・十字キー・ボタンを取得 |
| [EspUsbHostHIDVendor](examples/HID/EspUsbHostHIDVendor/) | ベンダーHID入力と出力/フィーチャーレポートの送信 |
| [EspUsbHostHIDRawDump](examples/HID/EspUsbHostHIDRawDump/) | デバイスアドレス付きでHexダンプ（複数デバイス対応） |

### Info

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostDeviceInfo](examples/Info/EspUsbHostDeviceInfo/) | 接続中の全デバイスのディスクリプタ・インターフェース・エンドポイントを表示 |
| [EspUsbHostCustomDeviceCallbacks](examples/Info/EspUsbHostCustomDeviceCallbacks/) | 接続・切断コールバックを自分で定義し、接続デバイスを調べる |

### MIDI

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostMIDI](examples/MIDI/EspUsbHostMIDI/) | USB MIDI入出力 |

### Audio

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostAudioInput](examples/Audio/EspUsbHostAudioInput/) | USB AudioのIsochronous INペイロードを受信 |
| [EspUsbHostAudioMp3](examples/Audio/EspUsbHostAudioMp3/) | 埋め込みMP3素材をデコードしてUSB Audio出力へ再生 |
| [EspUsbHostAudioOutput](examples/Audio/EspUsbHostAudioOutput/) | USB AudioのIsochronous OUTペイロードを送信 |

### Serial

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostUSBSerial](examples/Serial/EspUsbHostUSBSerial/) | CDC ACM・VCPシリアルの双方向ブリッジ |
| [EspUsbHostMultiUSBSerial](examples/Serial/EspUsbHostMultiUSBSerial/) | FTDIとCP210xのUSBシリアルデバイスを同時利用 |

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
void espUsbHostPrintDeviceConnected(const EspUsbHostDeviceInfo &device);
void espUsbHostPrintDeviceDisconnected(const EspUsbHostDeviceInfo &device);
```

コールバックは`const EspUsbHostDeviceInfo &device`を受け取ります。主要フィールド：`address`、`vid`、`pid`、`product`、`manufacturer`、`serial`、`speed`、`parentAddress`、`portId`。
標準的なシリアル表示だけでよいサンプルでは、`espUsbHostPrintDeviceConnected`と`espUsbHostPrintDeviceDisconnected`をそのまま登録できます。

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
void espUsbHostPrintHIDInput(const EspUsbHostHIDInput &input);
void espUsbHostPrintKeyboardEvent(const EspUsbHostKeyboardEvent &event);
```

主なイベントフィールド：

| コールバック | 主要フィールド |
|-------------|--------------|
| `onKeyboard` | `pressed`、`keycode`、`ascii`、`modifiers`、`address` |
| `onMouse` | `x`、`y`、`wheel`、`buttons`、`previousButtons`、`moved`、`buttonsChanged`、`address` |
| `onConsumerControl` | `pressed`、`usage`（16ビットHIDユーセージ）、`address` |
| `onSystemControl` | `pressed`、`usage`（8ビット）、`address` |
| `onGamepad` | `x`、`y`、`z`、`rz`、`rx`、`ry`、`hat`、`buttons`、`previousButtons`、`address` |
| `onHIDInput` / `onVendorInput` | `address`、`interfaceNumber`、`subclass`、`protocol`、`data`、`length` |

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
bool    setDtr(bool enable);
bool    setRts(bool enable);
void    setAddress(uint8_t address);
uint8_t address() const;
void    clearAddress();
```

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
bool audioReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool audioOutputReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool setAudioSampleRate(uint32_t sampleRate,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioSend(const uint8_t *data, size_t length,
               uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
size_t getAudioStreams(uint8_t address, EspUsbHostAudioStreamInfo *streams,
                       size_t maxStreams) const;
bool espUsbHostAudioStreamSupportsSampleRate(const EspUsbHostAudioStreamInfo &stream,
                                             uint32_t sampleRate);
bool espUsbHostAudioStreamMatchesPcm(const EspUsbHostAudioStreamInfo &stream,
                                     uint8_t channels,
                                     uint8_t bytesPerSample,
                                     uint8_t bitsPerSample,
                                     uint32_t sampleRate);
```

`onAudioData`はUSB Audio StreamingのIsochronous INエンドポイントから受信した生ペイロードを通知します。コールバックは`const EspUsbHostAudioData &audio`を受け取り、フィールドは`address`、`interfaceNumber`、`data`、`length`です。

`getAudioStreams`はストリーミングエンドポイントの方向、エンドポイントパケットサイズ、取得できた場合はUAC1 Type Iフォーマット情報を返します。離散サンプルレートまたは連続サンプルレート範囲も取得できます。`espUsbHostAudioStreamSupportsSampleRate`はその情報を使って指定サンプルレートに対応しているか判定し、`espUsbHostAudioStreamMatchesPcm`はチャンネル数、サンプルサイズ、ビット深度、サンプルレートをまとめて判定します。`setAudioSampleRate`はAudio Streamingエンドポイントを有効化するときに送るUAC1 sampling frequencyリクエストの値を設定します。`audioSend`はUSB Audio StreamingのIsochronous OUTエンドポイントへ生PCMペイロードを送信します。

### デバイス探索

```cpp
size_t deviceCount() const;
size_t getDevices(EspUsbHostDeviceInfo *devices, size_t maxDevices) const;
bool   getDevice(uint8_t address, EspUsbHostDeviceInfo &device) const;
size_t getInterfaces(uint8_t address, EspUsbHostInterfaceInfo *interfaces,
                     size_t maxInterfaces) const;
size_t getEndpoints(uint8_t address, EspUsbHostEndpointInfo *endpoints,
                    size_t maxEndpoints) const;
void   printDeviceInfo(uint8_t address, bool includeHubInfo = false,
                       Print &out = Serial);
void   printAllDeviceInfo(Print &out = Serial);
```

配列サイズ定数：`ESP_USB_HOST_MAX_DEVICES`、`ESP_USB_HOST_MAX_INTERFACES`、`ESP_USB_HOST_MAX_ENDPOINTS`。

### エラーハンドリング

```cpp
int         lastError() const;
const char *lastErrorName() const;
```

## 設計方針

**コールバック登録スタイル。** `onKeyboard()`・`onMouse()` などにラムダや関数を登録して使います。`EspUsbHost` を継承して仮想関数をオーバーライドする旧スタイルは主要APIではありません。

**破壊的変更を許容。** 旧来の継承ベースのAPIとの後方互換性よりも、クリーンなArduino向けAPIを優先します。

**非ゴール：**
- すべてのHIDレポートディスクリプタを完全自動解釈すること
- 初回リリースですべてのUSBクラスを実装すること
- ESP-IDF HID Host Driver APIとの互換性
- USBスペックの内部仕様をそのままArduino APIとして公開すること

## 複数デバイスの扱い

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

- [`tests/peer/`](tests/peer/) — ESP32-S3をデバイス側として使う2台構成のUSBテスト
- [`tests/loopback/`](tests/loopback/) — 1台でのループバックテスト
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
