# EspUsbHost 仕様方針

このドキュメントは、EspUsbHostを全面的に作り直すための利用者向け仕様方針をまとめるものです。
内部実装の詳細、ESP-IDF由来の構造、クラスドライバの分割方法はここでは扱いません。

このファイルは、要件、設計方針、利用者体験、対応範囲、制約を共有するためのものです。
本文中のAPI名、構造体名、フィールド名、コード例は、方針を説明するための案であり、最終的な確定仕様ではありません。
実際の命名、型定義、引数、クラス構成は、実装フェーズでコードとの整合を見ながら確定します。

## 前提方針

新しいEspUsbHostは、既存APIとの互換性維持よりも、きれいでシンプルなArduino向けAPIを優先します。

現行ライブラリは古いコードをArduino向けに移植・拡張してきた経緯があるため、内部構造や公開APIに古い前提が残っています。
今回の作り直しでは、破壊的変更を許容し、利用者から見える形を整理します。

特に次の方針を優先します。

- `loop()` でUSB処理関数を呼ばなくてよいこと
- 継承ベースではなく、コールバック登録やArduino標準の `Stream` / `Print` 互換を使うこと
- HID専用ではなく、HUB、CDC、MSC、UAC、UVC、USB MIDIなどを同じ考え方で扱えること
- 低レベル情報やデバッグログは維持しつつ、通常利用のAPIは短く分かりやすいこと
- 旧API互換のためだけの複雑さを持ち込まないこと

## 目的

EspUsbHostは、ESP32のUSB Host機能をArduinoから簡単に使うためのライブラリです。

Arduino利用者がUSBの細かい仕様を意識しなくても、HID入力、シリアル通信、ストレージ、音声、映像、MIDIなどを短いスケッチで扱えることを優先します。

設計では次を重視します。

- Arduinoスケッチから見てシンプルであること
- `setup()` と `loop()` に自然に収まること
- 継承よりもコールバック登録を基本にすること
- よく使うUSBクラスは高レベルAPIで扱えること
- クラス固有のrawデータやdescriptorも必要に応じて扱えること
- HID以外のUSBクラス追加を前提にすること
- 破壊的変更を許容し、古いAPIに引きずられないこと

## 基本方針

利用者側の基本形は、グローバルに `EspUsbHost` を作成し、`setup()` で必要なコールバックを登録して `begin()` する形にします。
ライブラリはバックグラウンドのFreeRTOS taskを作成してUSBイベントを処理するため、通常のスケッチでは `loop()` からUSB処理用の関数を呼ぶ必要はありません。

```cpp
#include "EspUsbHost.h"

EspUsbHost usb;
EspUsbHostCdcSerial CdcSerial(usb);

void setup() {
  Serial.begin(115200);

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
    if (event.pressed && event.ascii) {
      Serial.print((char)event.ascii);
    }
  });

  usb.onMouse([](const EspUsbHostMouseEvent &event) {
    Serial.printf("x=%d y=%d wheel=%d buttons=%02x\n",
                  event.x, event.y, event.wheel, event.buttons);
  });

  CdcSerial.begin(115200);

  usb.begin();
}

void loop() {
  while (CdcSerial.available()) {
    Serial.write(CdcSerial.read());
  }
}
```

継承してvirtual関数をoverrideする使い方は、新仕様では基本APIにしません。

## 公開APIの考え方

公開APIは、Arduino利用者が自然に使うものだけに絞ります。

最低限の基本API:

```cpp
bool begin();
bool begin(const EspUsbHostConfig &config);
void end();
bool ready() const;
```

タスク設定:

```cpp
struct EspUsbHostConfig {
  uint32_t taskStackSize = 4096;
  UBaseType_t taskPriority = 5;
  BaseType_t taskCore = tskNO_AFFINITY;
};
```

イベント登録API:

```cpp
void onDeviceConnected(DeviceCallback callback);
void onDeviceDisconnected(DeviceCallback callback);
void onInterfaceReady(InterfaceCallback callback);
void onHubPortChanged(HubPortCallback callback);

void onKeyboard(KeyboardCallback callback);
void onMouse(MouseCallback callback);
void onGamepad(GamepadCallback callback);

void onHIDInput(HIDInputCallback callback);
void onHIDReportDescriptor(HIDReportDescriptorCallback callback);

bool sendHIDReport(uint8_t interfaceNumber,
                   uint8_t reportType,
                   uint8_t reportId,
                   const uint8_t *data,
                   size_t length);
bool setKeyboardLeds(bool numLock, bool capsLock, bool scrollLock);

bool setHubPortPower(uint8_t hubAddress, uint8_t portNumber, bool enable);
bool resetHubPort(uint8_t hubAddress, uint8_t portNumber);

void onCdcData(CdcDataCallback callback);
void onMidiMessage(MidiMessageCallback callback);
void onMscMounted(MscMountedCallback callback);
void onMscUnmounted(MscUnmountedCallback callback);
void onAudioInput(AudioInputCallback callback);
void onAudioOutputRequest(AudioOutputRequestCallback callback);
void onVideoFrame(VideoFrameCallback callback);
```

`begin()` は初期化に成功したら `true` を返します。
`begin()` はUSB Host機能を初期化し、バックグラウンドtaskを開始します。
`end()` はUSB Host機能を終了し、確保したリソースを解放します。
`ready()` はUSB Hostが利用可能な状態かを返します。

## クラス対応方針

EspUsbHostはHID専用ライブラリではなく、複数のUSBクラスを同じ使い方で扱えるUSB Hostライブラリとします。

クラスごとのAPIは、Arduino利用者が触る頻度の高い操作だけを高レベルにします。
細かい制御が必要な場合は、クラス別のrawイベントや低レベル送受信APIで補えるようにします。

対応を見据えるUSBクラス:

| USBクラス | 目的 | API方針 |
| --- | --- | --- |
| HID | キーボード、マウス、ゲームパッド、バーコードリーダなど | `onKeyboard()`、`onMouse()`、`onGamepad()`、`onHIDInput()` |
| CDC ACM | USBシリアル、モデム系デバイス | `EspUsbHostCdcSerial`、`onCdcData()` |
| MSC | USBメモリ、カードリーダ | mount/unmountイベント、ファイルシステム連携 |
| UAC | USBオーディオ | 音声入力/出力ストリーム |
| UVC | USBカメラ | フレーム受信イベント |
| USB MIDI | MIDIキーボード、MIDIコントローラ、音源 | `onMidiMessage()`、`midiSend()` |
| HUB | USBハブ、複数デバイス接続、ポート電源管理 | device path、port event、port power control |

CDC、MSC、UAC、UVCはESP-IDF側またはesp-usb側にHost実装やサンプルがあるため、挙動確認やAPI設計の参考にします。
USB MIDIはCDCに近い「エンドポイントでデータを送受信する周辺機器」として扱い、ArduinoからはMIDIメッセージ単位で使えるAPIを優先します。
HUBは複数デバイス対応の基盤として扱い、対応可能なHubではPort Power Switchingによるポート単位の電源制御も提供します。

どのクラスを追加しても、利用者側の基本形は `EspUsbHost usb;`、`usb.onXxx(...)`、`usb.begin()` に統一します。

## 実装優先度

実装は、利用頻度、Arduino API化のしやすさ、既存サンプルや既存ドライバの有無を基準に進めます。

推奨順:

1. HID再設計
2. HUB基本対応
3. CDC ACM / VCP
4. USB MIDI
5. MSC
6. UAC
7. UVC

HIDは現行機能の置き換えとして最初に整理します。
HUBは複数デバイス対応とポート電源管理の土台になるため、HIDの次に対応します。
CDCはSerial風APIにしやすく、既存のArduinoスケッチやライブラリと接続しやすいため早い段階で対応します。
USB MIDIはHost側の既存ドライバはないものの、bulk転送を中心に小さく始められるためCDCの次に扱います。
MSCはVFS/FAT連携が必要で、UACとUVCはバッファ、帯域、メモリ制約が大きいため後段で対応します。

## デバイスイベント

USBデバイスの接続と切断は、必要な利用者だけが扱えるようにします。

利用者callbackはライブラリのUSBイベント処理taskから呼ばれます。
callback内で短い `Serial.print()` 程度は許容しますが、長時間ブロックする処理、重いファイル操作、長いdelayは避けます。
必要に応じて、callback内ではフラグやキューへ渡し、重い処理は利用者側の `loop()` や別taskで行います。

```cpp
usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
  Serial.printf("connected: vid=%04x pid=%04x\n", device.vid, device.pid);
});

usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
  Serial.println("disconnected");
});
```

`EspUsbHostDeviceInfo` には、利用者がログや分岐に使いやすい情報だけを含めます。

想定フィールド:

```cpp
struct EspUsbHostDeviceInfo {
  uint8_t address;
  uint16_t vid;
  uint16_t pid;
  const char *manufacturer;
  const char *product;
  const char *serial;
};
```

文字列情報が取得できない場合は空文字列にします。

## HUB

USB Hubをサポートし、複数デバイスの同時接続を扱えるようにします。
Hub経由のデバイスでも、利用者側の基本APIは直接接続時と同じにします。

Hub対応では、デバイスをUSB addressだけでなく、Hubとportを含む接続位置でも識別できるようにします。

想定フィールド:

```cpp
struct EspUsbHostDeviceInfo {
  uint8_t address;
  uint8_t parentHubAddress;
  uint8_t parentPort;
  uint8_t depth;
  uint16_t vid;
  uint16_t pid;
  const char *manufacturer;
  const char *product;
  const char *serial;
};
```

Hub portの状態変化を受け取るAPIを用意します。

```cpp
usb.onHubPortChanged([](const EspUsbHostHubPortEvent &event) {
  Serial.printf("hub=%u port=%u connected=%d powered=%d\n",
                event.hubAddress,
                event.portNumber,
                event.connected,
                event.powered);
});
```

Port Power Switching対応Hubでは、ポート単位の電源ON/OFFを扱えるようにします。
これはデバイスの論理的な切り離し、再接続、復旧処理に使えます。

```cpp
usb.setHubPortPower(hubAddress, portNumber, false);
delay(500);
usb.setHubPortPower(hubAddress, portNumber, true);
```

想定API:

```cpp
bool setHubPortPower(uint8_t hubAddress, uint8_t portNumber, bool enable);
bool resetHubPort(uint8_t hubAddress, uint8_t portNumber);
```

Port Power SwitchingにはHub側の対応が必要です。
Hub descriptorでポート電源制御に対応していないHubでは、APIは `false` を返し、Warnログを出します。

Hub対応ではendpoint数、デバイス数、電源供給、Hub段数の制約を明示します。
初期対応では1段Hubを優先し、複数段HubはUSB Host stackとメモリ/endpoint制約に合わせて段階的に扱います。

## キーボード

キーボードは、キー単位のイベントとして扱えることを優先します。

```cpp
usb.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
  if (event.pressed) {
    Serial.printf("key=%02x ascii=%c\n", event.keycode, event.ascii);
  }
});
```

想定フィールド:

```cpp
struct EspUsbHostKeyboardEvent {
  uint8_t interfaceNumber;
  bool pressed;
  bool released;
  uint8_t keycode;
  uint8_t ascii;
  uint8_t modifiers;
};
```

`pressed` は押された瞬間、`released` は離された瞬間を表します。
`ascii` は変換できる場合のみ値を持ち、変換できないキーでは `0` にします。

同時押し、修飾キー、キーリピートの扱いは、Arduino利用者が理解しやすいイベントとして提供します。

キーボード配列は設定可能にします。

```cpp
usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_US);
usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_JP);
```

初期値はUS配列とします。

## マウス

マウスは移動量、ホイール、ボタン状態をまとめて通知します。

```cpp
usb.onMouse([](const EspUsbHostMouseEvent &event) {
  if (event.moved) {
    Serial.printf("move x=%d y=%d wheel=%d\n", event.x, event.y, event.wheel);
  }
  if (event.buttonsChanged) {
    Serial.printf("buttons=%02x\n", event.buttons);
  }
});
```

想定フィールド:

```cpp
struct EspUsbHostMouseEvent {
  uint8_t interfaceNumber;
  int16_t x;
  int16_t y;
  int16_t wheel;
  uint8_t buttons;
  uint8_t previousButtons;
  bool moved;
  bool buttonsChanged;
};
```

ボタン定数はわかりやすい名前で提供します。

```cpp
ESP_USB_HOST_MOUSE_LEFT
ESP_USB_HOST_MOUSE_RIGHT
ESP_USB_HOST_MOUSE_MIDDLE
ESP_USB_HOST_MOUSE_BACK
ESP_USB_HOST_MOUSE_FORWARD
```

## ゲームパッド

ゲームパッドはHID拡張の主要ターゲットとします。
ただしデバイスごとの差が大きいため、最初からすべてのゲームパッドを完全に正規化することは目標にしません。

高レベルAPIでは、一般的なボタン、スティック、トリガーを扱える形を目指します。

```cpp
usb.onGamepad([](const EspUsbHostGamepadEvent &event) {
  Serial.printf("lx=%d ly=%d buttons=%08x\n",
                event.leftX, event.leftY, event.buttons);
});
```

raw HID APIと併用できるようにし、未対応デバイスでも利用者がreportを確認できるようにします。

## Raw HID

HIDの拡張やデバッグのため、raw input reportを受け取れるAPIを用意します。

```cpp
usb.onHIDInput([](const EspUsbHostHIDInput &input) {
  Serial.printf("iface=%u protocol=%u len=%u\n",
                input.interfaceNumber,
                input.protocol,
                input.length);

  for (size_t i = 0; i < input.length; i++) {
    Serial.printf("%02x ", input.data[i]);
  }
  Serial.println();
});
```

想定フィールド:

```cpp
struct EspUsbHostHIDInput {
  uint8_t address;
  uint8_t interfaceNumber;
  uint8_t subclass;
  uint8_t protocol;
  const uint8_t *data;
  size_t length;
};
```

`data` はコールバック中だけ有効です。
利用者が後で使う場合は、自分でコピーします。

## CDC

CDC ACMはUSBシリアルとして扱える高レベルAPIを用意します。
主APIは `Stream` / `Print` 互換の `EspUsbHostCdcSerial` とし、Arduinoの `Serial` に近い使い方を優先します。

```cpp
EspUsbHost usb;
EspUsbHostCdcSerial CdcSerial(usb);

void setup() {
  Serial.begin(115200);
  CdcSerial.begin(115200);
  usb.begin();
}

void loop() {
  if (CdcSerial.available()) {
    Serial.write(CdcSerial.read());
  }

  if (Serial.available()) {
    CdcSerial.write(Serial.read());
  }
}
```

`EspUsbHostCdcSerial` は `Stream` を継承します。
そのため、`Stream &` や `Print &` を受け取る既存コードに渡せることを目標にします。

```cpp
void bridge(Stream &from, Print &to) {
  while (from.available()) {
    to.write(from.read());
  }
}

bridge(Serial, CdcSerial);
bridge(CdcSerial, Serial);
```

想定API:

```cpp
class EspUsbHostCdcSerial : public Stream {
public:
  explicit EspUsbHostCdcSerial(EspUsbHost &host);

  bool begin(uint32_t baud = 115200);
  void end();
  bool connected() const;

  int available() override;
  int read() override;
  int peek() override;
  void flush() override;
  size_t write(uint8_t data) override;
  size_t write(const uint8_t *buffer, size_t size) override;

  using Print::write;

  bool setBaudRate(uint32_t baud);
  bool setDtr(bool enable);
  bool setRts(bool enable);
};
```

`HardwareSerial &` 固定のAPIへ完全互換で渡すことは目標にしません。
Arduinoでは `Stream &` または `Print &` を受けるライブラリとの互換を優先します。

イベント式APIも併用可能にします。

```cpp
usb.onCdcData([](const EspUsbHostCdcData &data) {
  Serial.write(data.data, data.length);
});
```

想定フィールド:

```cpp
struct EspUsbHostCdcData {
  uint8_t address;
  uint8_t interfaceNumber;
  const uint8_t *data;
  size_t length;
};
```

`EspUsbHost` 直下にも簡易送信APIを用意してよいですが、CDCの通常利用では `EspUsbHostCdcSerial` を推奨します。

```cpp
size_t cdcWrite(const uint8_t *data, size_t length);
size_t cdcWrite(const char *text);
```

複数のCDCデバイスや複数interfaceが接続される場合に備え、将来的にはinterface指定付きAPIも提供できるようにします。

```cpp
size_t cdcWrite(uint8_t interfaceNumber, const uint8_t *data, size_t length);
```

## USB MIDI

USB MIDIは、バイト列ではなくMIDIメッセージ単位で扱えることを優先します。

```cpp
usb.onMidiMessage([](const EspUsbHostMidiMessage &message) {
  Serial.printf("status=%02x data1=%02x data2=%02x\n",
                message.status, message.data1, message.data2);
});

usb.midiSendNoteOn(0, 60, 100);
usb.midiSendNoteOff(0, 60, 0);
```

想定フィールド:

```cpp
struct EspUsbHostMidiMessage {
  uint8_t address;
  uint8_t cable;
  uint8_t status;
  uint8_t data1;
  uint8_t data2;
  const uint8_t *raw;
  size_t length;
};
```

基本送信API:

```cpp
bool midiSend(const uint8_t *data, size_t length);
bool midiSendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
bool midiSendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);
bool midiSendControlChange(uint8_t channel, uint8_t control, uint8_t value);
```

## MSC

MSCは、USBストレージをArduinoから扱えることを目標にします。
ファイルシステムとの接続方法はボード環境やArduino-ESP32の提供機能に影響されるため、利用者側APIはmount/unmountイベントを中心にします。

```cpp
usb.onMscMounted([](const EspUsbHostMscVolume &volume) {
  Serial.println("storage mounted");
});

usb.onMscUnmounted([](const EspUsbHostMscVolume &volume) {
  Serial.println("storage removed");
});
```

初期段階では、MSCは他クラスより後回しでもよいです。
ただしAPI設計上は、ストレージのように「接続後に継続的な入出力をするクラス」も扱える形にします。

## UAC

UACはUSBオーディオの入力または出力を扱うための拡張候補です。

```cpp
usb.onAudioInput([](const EspUsbHostAudioBuffer &audio) {
  // PCM samples
});
```

サンプルレート、チャンネル数、サンプル形式などの設定が必要になるため、HIDやCDCよりも設定APIが増えることを許容します。

## UVC

UVCはUSBカメラのフレームを扱うための拡張候補です。

```cpp
usb.onVideoFrame([](const EspUsbHostVideoFrame &frame) {
  Serial.printf("frame %ux%u len=%u\n",
                frame.width, frame.height, frame.length);
});
```

ESP32のメモリ制約が大きいため、初期仕様では低解像度、MJPEG、フレーム単位またはチャンク単位の受信を想定します。
UVCは高レベルに見せつつも、バッファサイズや解像度などの制約を明示する方針にします。

## HID Report Descriptor

HID report descriptorを確認したい利用者向けに、取得できたdescriptorを通知するAPIを用意します。

```cpp
usb.onHIDReportDescriptor([](const EspUsbHostHIDReportDescriptor &report) {
  Serial.printf("iface=%u len=%u\n", report.interfaceNumber, report.length);
});
```

想定フィールド:

```cpp
struct EspUsbHostHIDReportDescriptor {
  uint8_t address;
  uint8_t interfaceNumber;
  const uint8_t *data;
  size_t length;
};
```

`data` はコールバック中だけ有効です。

## HID出力

HID出力は初期仕様から対応します。
キーボードLED制御は軽量で利用頻度も高いため、高レベルAPIとして最初から提供します。

```cpp
usb.setKeyboardLeds(true, false, false);  // NumLock, CapsLock, ScrollLock
```

より細かい制御のため、HID class requestの `Set_Report` 相当を送れるAPIも用意します。

```cpp
bool sendHIDReport(uint8_t interfaceNumber,
                   uint8_t reportType,
                   uint8_t reportId,
                   const uint8_t *data,
                   size_t length);
```

`sendHIDReport()` はOutput reportとFeature reportの送信に使います。
`reportType` はInput、Output、FeatureのHID report typeを指定します。
通常のキーボードLED制御では、利用者は `sendHIDReport()` を直接呼ばず `setKeyboardLeds()` を使います。

```cpp
uint8_t leds = ESP_USB_HOST_KEYBOARD_LED_NUM_LOCK |
               ESP_USB_HOST_KEYBOARD_LED_CAPS_LOCK;

usb.sendHIDReport(interfaceNumber,
                  ESP_USB_HOST_HID_REPORT_TYPE_OUTPUT,
                  0,
                  &leds,
                  1);
```

LED定数:

```cpp
ESP_USB_HOST_KEYBOARD_LED_NUM_LOCK
ESP_USB_HOST_KEYBOARD_LED_CAPS_LOCK
ESP_USB_HOST_KEYBOARD_LED_SCROLL_LOCK
ESP_USB_HOST_KEYBOARD_LED_COMPOSE
ESP_USB_HOST_KEYBOARD_LED_KANA
```

初期対応ではBoot keyboardのLED制御を優先します。
Report protocolのキーボードや複合HIDでは、report descriptorに基づく完全自動解釈が必要になる場合があるため、raw output APIで補えるようにします。

## ログ出力

ライブラリのデバッグ情報は、Arduino-ESP32のログレベルに従って出力します。
個別機能ごとの明示的なON/OFF APIは増やしすぎず、通常はログレベルで制御します。

既存実装は、USB device descriptor、configuration descriptor、interface descriptor、endpoint descriptor、HID descriptor、transfer状態などをかなり細かくログ出力しています。
新実装でもこの粒度は維持し、機能追加や内部整理によってデバッグ時に見える情報量を減らさない方針にします。

ログレベルの目安:

| ログレベル | 出力内容 |
| --- | --- |
| Error | 初期化失敗、デバイスopen失敗、復旧不能な転送エラー |
| Warn | 対応外descriptor、バッファ不足、一部機能の無効化 |
| Info | デバイス接続/切断、VID/PID、interface概要、mount/open/close |
| Debug | descriptor詳細、class request、状態遷移 |
| Verbose | raw transfer、PCAP TEXT、詳細な送受信ダンプ |

維持するログ情報:

- USB Host library/clientのinstall、uninstall、event handling結果
- device接続/切断、device address、speed、VID/PID、文字列descriptor
- device/configuration/interface/endpoint/class-specific descriptorの主要フィールド
- interface claim/release、endpoint clear/halt/flush、transfer alloc/free/submitの結果
- control transferのrequest/response概要
- interrupt/bulk/isoc transferのendpoint、byte count、status
- HID report descriptorの取得結果と、可能な範囲でのHID item解析
- class driverごとのopen/close/start/stop、mount/unmount、stream開始/停止

ログは人が読める説明形式を維持しつつ、PCAP TEXTのようにツール変換を想定する出力は形式を崩さないようにします。

### PCAP TEXT

既存機能として、USB送受信内容をPCAP風のテキストで出力する `PCAP TEXT` ダンプを維持します。
`PCAP TEXT` はログ出力の一種として扱い、Verboseログレベルで出力します。

この出力は `text2pcap` などでpcap形式へ変換し、Wiresharkで確認できることを目的にします。
実機上でUSBアナライザなどから取得したpcapと比較することで、ライブラリの挙動確認やデバッグに使えるようにします。

出力対象:

- ライブラリが直接発行したcontrol transfer
- ライブラリが直接発行したinterrupt/bulk/isoc transfer
- 受信したtransfer callbackの内容
- descriptor取得やHID report取得など、ライブラリが把握できる範囲のUSB request/response

PCAP TEXTには、実際にコールバックで確認できた受信データと、ライブラリ側が発行した送信データを出力します。
ただしUSB Hostスタックや下位ドライバが内部で発行する送信、再送、ACK/NAK、バスレベルの細かい挙動まではライブラリ側で完全には把握できません。

そのため、出力には種類を区別できる情報を含めます。

- `observed`: transfer callbackなどで実際にライブラリが受け取ったもの
- `submitted`: ライブラリがUSB Host APIへ送信依頼したもの
- `inferred`: ライブラリがdescriptor情報やAPI呼び出しから推定して記録したもの

`inferred` は、USB Hostスタックの内部処理まで含めた完全な実測ではありません。
Wiresharkで実機pcapと比較する場合は、この区別を前提にします。

PCAP TEXTはデバッグ用機能であり、通常利用ではVerboseログを無効にすることで出力されないようにします。
出力処理が重くなるため、リアルタイム性が必要なUAC/UVCなどではVerboseログを有効にした状態での動作性能は保証しません。

## エラー処理

Arduino向けには、通常利用で細かいエラーコードを意識しなくてよい形にします。

基本APIは `bool` で成功/失敗を返します。
詳細を見たい利用者向けには、最後のエラーを取得するAPIを用意します。

```cpp
int lastError() const;
const char *lastErrorName() const;
```

致命的ではない転送エラーや一時的な切断処理は、ライブラリ側で可能な限り回復します。

## 対応対象

初期ターゲット:

- ESP32-S3
- Arduino-ESP32のUSB Host APIが利用できるボード

初期対応USBクラス:

- HID Keyboard
- HID Mouse
- HID raw input
- HID output report
- Keyboard LED control

拡張候補:

- HUB
- Hub Port Power Switching
- HID Gamepad
- HID Consumer Control
- HID Barcode Scanner
- CDC ACM
- USB MIDI
- MSC
- UAC
- UVC

HID以外を追加しても、利用者側の基本形は `begin()` と `onXxx()` のまま維持します。

## 旧APIとの関係

新仕様では破壊的変更を許容します。

旧APIである継承ベースのvirtual callbackは、基本仕様には含めません。

旧API例:

```cpp
class MyEspUsbHost : public EspUsbHost {
  void onKeyboardKey(uint8_t ascii, uint8_t keycode, uint8_t modifier) {
  }
};
```

新仕様では次の形に置き換えます。

```cpp
EspUsbHost usb;

usb.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
});
```

移行を助けるために、一時的な互換レイヤーや移行ガイドを用意してもよいですが、長期的にはcallback登録式を正式APIとします。

## サンプル方針

サンプルは、Arduino利用者がすぐ動かせる短いものを中心にします。

用意するサンプル:

- `Keyboard`
- `Mouse`
- `KeyboardAndMouse`
- `HIDRawDump`
- `Gamepad`
- `CdcSerial`
- `Midi`
- `Msc`
- `Audio`
- `Camera`

サンプルでは、USB descriptorやESP-IDFの概念を前面に出しません。
必要な場合だけraw dumpサンプルやクラス別dumpサンプルで確認できるようにします。

## 命名方針

Arduinoスケッチで読みやすい名前を優先します。

- クラス名は `EspUsbHost`
- 構造体名は `EspUsbHostKeyboardEvent` のように用途を明示する
- メソッド名は `onKeyboard()`、`onMouse()`、`onCdcData()`、`onMidiMessage()` のように短くする
- 定数は `ESP_USB_HOST_` prefixを付ける

ESP-IDFの型名やHID仕様上の細かい名前は、必要な場面を除いて公開APIに出しません。

## 非目標

初期仕様では次を目標にしません。

- すべてのHIDデバイスを完全に自動解釈すること
- すべてのUSBクラスを最初のリリースで実装すること
- ESP-IDFのHID Host Driver API互換にすること
- USB仕様の全機能をそのままArduino APIへ露出すること
- descriptor parserの詳細を利用者に意識させること
- USBバス上の全パケットを完全な実測pcapとして記録すること

## まとめ

新しいEspUsbHostは、ESP-IDF風のコンポーネント構造ではなく、Arduinoスケッチから扱いやすいイベント駆動APIを中心にします。

高レベルには `onKeyboard()`、`onMouse()`、`onGamepad()`、`onCdcData()`、`onMidiMessage()` などを提供し、拡張やデバッグにはクラス別rawイベントとPCAP TEXT出力を提供します。

内部は全面的に作り直してよいものとし、利用者側には「作る、コールバックを登録する、beginする、taskを呼ぶ」という単純な使い方を提供します。
