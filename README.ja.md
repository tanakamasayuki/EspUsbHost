# EspUsbHost

ESP32-S3でUSB Hostを使うためのArduinoライブラリです。

USB処理はバックグラウンドのFreeRTOSタスクで行われるため、`loop()`でUSBポーリング関数を呼ぶ必要はありません。`setup()`でコールバックを登録して`begin()`を呼ぶだけで動作します。

## 機能

- **HID入力** — キーボード・マウス・コンシューマーコントロール（メディアキー）・システムコントロール（電源/スタンバイ）・ゲームパッド
- **HID出力** — キーボードLED制御・ベンダー出力/フィーチャーレポート
- **CDC ACM** — `EspUsbHostCdcSerial`によるUSBシリアル入出力（Arduino `Stream`/`Print` 互換）
- **MIDI** — USB MIDI入出力
- **デバイス探索** — 接続デバイス・インターフェース・エンドポイントの列挙
- **複数デバイス対応** — 各コールバックと送信APIにオプションの`address`引数があり、特定デバイスを指定可能

## 必要環境

- ESP32-S3、またはArduino-ESP32 USB Hostに対応したボード
- Arduino-ESP32コア

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
| [EspUsbHostMouse](examples/HID/EspUsbHostMouse/) | マウスの移動量とボタン操作を取得 |
| [EspUsbHostKeyboardLeds](examples/HID/EspUsbHostKeyboardLeds/) | NumLock・CapsLock・ScrollLock LEDを制御 |
| [EspUsbHostConsumerControl](examples/HID/EspUsbHostConsumerControl/) | メディアキー（音量・再生/一時停止など）を検出 |
| [EspUsbHostSystemControl](examples/HID/EspUsbHostSystemControl/) | システムキー（電源・スタンバイ・ウェイクアップ）を検出 |
| [EspUsbHostGamepad](examples/HID/EspUsbHostGamepad/) | ゲームパッドのスティック・十字キー・ボタンを取得 |
| [EspUsbHostHIDVendor](examples/HID/EspUsbHostHIDVendor/) | ベンダーHID入力と出力/フィーチャーレポートの送信 |
| [EspUsbHostCustomHID](examples/HID/EspUsbHostCustomHID/) | 任意のHID入力レポートをHexダンプ |
| [EspUsbHostHIDRawDump](examples/HID/EspUsbHostHIDRawDump/) | デバイスアドレス付きでHexダンプ（複数デバイス対応） |

### Info

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostDeviceInfo](examples/Info/EspUsbHostDeviceInfo/) | 接続中の全デバイスのディスクリプタ・インターフェース・エンドポイントを表示 |

### MIDI

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostMIDI](examples/MIDI/EspUsbHostMIDI/) | USB MIDI入出力 |

### Serial

| スケッチ | 説明 |
|----------|------|
| [EspUsbHostUSBSerial](examples/Serial/EspUsbHostUSBSerial/) | CDC ACMシリアルの双方向ブリッジ |

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
};
```

### デバイスイベント

```cpp
void onDeviceConnected(DeviceCallback callback);
void onDeviceDisconnected(DeviceCallback callback);
```

コールバックは`const EspUsbHostDeviceInfo &device`を受け取ります。主要フィールド：`address`、`vid`、`pid`、`product`、`manufacturer`、`serial`、`speed`、`parentAddress`、`parentPort`。

### HID入力

```cpp
void onKeyboard(KeyboardCallback callback);
void onMouse(MouseCallback callback);
void onConsumerControl(ConsumerControlCallback callback);
void onSystemControl(SystemControlCallback callback);
void onGamepad(GamepadCallback callback);
void onHIDInput(HIDInputCallback callback);    // 生データ — 全HIDインターフェースで発火
void onVendorInput(VendorInputCallback callback);
```

主なイベントフィールド：

| コールバック | 主要フィールド |
|-------------|--------------|
| `onKeyboard` | `pressed`、`keyCode`、`ascii`、`modifiers`、`address` |
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

キーボードレイアウト定数：`ESP_USB_HOST_KEYBOARD_LAYOUT_US`、`ESP_USB_HOST_KEYBOARD_LAYOUT_JP`。

### CDC ACM（USBシリアル）

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

### デバイス探索

```cpp
size_t deviceCount() const;
size_t getDevices(EspUsbHostDeviceInfo *devices, size_t maxDevices) const;
bool   getDevice(uint8_t address, EspUsbHostDeviceInfo &device) const;
size_t getInterfaces(uint8_t address, EspUsbHostInterfaceInfo *interfaces,
                     size_t maxInterfaces) const;
size_t getEndpoints(uint8_t address, EspUsbHostEndpointInfo *endpoints,
                    size_t maxEndpoints) const;
```

配列サイズ定数：`ESP_USB_HOST_MAX_DEVICES`、`ESP_USB_HOST_MAX_INTERFACES`、`ESP_USB_HOST_MAX_ENDPOINTS`。

### エラーハンドリング

```cpp
int         lastError() const;
const char *lastErrorName() const;
```

## 複数デバイスの扱い

送信APIと`EspUsbHostCdcSerial`はデフォルトで`ESP_USB_HOST_ANY_ADDRESS`を使用し、該当クラスの最初のデバイスを対象にします。特定のデバイスを指定する場合は、`EspUsbHostDeviceInfo::address`で取得したアドレスを渡します。

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

セットアップ方法は[tests/peer/README.md](tests/peer/README.md)を参照してください。
