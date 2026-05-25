# EspUsbHost

> 日本語版: [README.ja.md](README.ja.md)

Arduino library for using USB Host on ESP32-S3.

USB events are processed in a background FreeRTOS task, so `loop()` does not need to call any USB polling function. Register callbacks in `setup()`, call `begin()`, and the library handles the rest.

## Features

- **HID input** — keyboard, mouse, consumer control (media keys), system control (power/standby), gamepad
- **HID output** — keyboard LED control, vendor output/feature reports
- **USB serial** — CDC ACM and common VCP devices (FTDI, CP210x, CH34x) via `EspUsbHostCdcSerial` (Arduino `Stream`/`Print` compatible)
- **MIDI** — USB MIDI input and output
- **USB audio** — raw isochronous IN payloads and isochronous OUT writes for USB Audio streaming interfaces
- **MSC block I/O** — USB Mass Storage Bulk-Only Transport with SCSI capacity/read/write block access
- **Device discovery** — enumerate connected devices, interfaces, and endpoints
- **Multiple devices** — each callback and send API accepts an optional `address` parameter to target a specific device

## Roadmap

### USB class support

| Class | Status |
|-------|--------|
| HID — keyboard, mouse, gamepad, consumer control, system control, vendor | ✅ Done |
| USB serial — CDC ACM and VCP (FTDI, CP210x, CH34x) via `EspUsbHostCdcSerial`; baud, data bits, parity, and stop bits are configurable | ✅ Done |
| USB MIDI | ✅ Done |
| UAC — USB audio input/output | 🔲 Experimental |
| HUB — hub detection, topology info, and port power control | 🔲 Partial; `hub_info` and `hub_power` manual tests pass |
| MSC — USB storage block I/O | 🔲 Experimental |
| UVC — USB camera | 💭 Under consideration |

### Other planned features

| Feature | Status |
|---------|--------|
| `onHIDReportDescriptor()` — HID report descriptor access | ✅ Done |
| HID gamepad input — descriptor-decoded fields plus raw/report bytes for user-defined mapping | ✅ Mappable event API; mapping helpers still under consideration |
| Loopback tests (ESP32-P4 single-board) | 🔲 In progress |
| Manual tests — VCP serial, multi-device, hot-plug | 🔲 In progress |

## Requirements

- ESP32-S3, or any board supported by Arduino-ESP32 USB Host
- Arduino-ESP32 core

### ESP32-P4 notes

ESP32-P4 boards can expose up to four USB-related connectors or paths, depending on the board design:

- External USB-to-UART serial converter, such as CH34x, for flashing/logging
- USB FS used as CDC
- USB FS OTG
- USB HS OTG

Some boards also have a normal UART port behind the external USB-to-UART converter. This is separate from the ESP32-P4 built-in USB Serial/JTAG and USB OTG peripherals.

The chip-level signal pins are as follows. Board connectors may be wired differently, so always check the board schematic before wiring or choosing a port.

| Typical ESP32-P4 role | D- | D+ | Notes |
|------------------------|----|----|-------|
| USB CDC FS / USB Serial/JTAG FS | GPIO24 | GPIO25 | Commonly used for built-in USB Serial/JTAG or a FS device-side CDC connector. This connector is easy to confuse with USB Host on some boards. |
| USB OTG FS | GPIO26 | GPIO27 | Commonly used as the full-speed OTG connector; selectable as USB Host with `ESP_USB_HOST_PORT_FULL_SPEED`. |
| USB OTG HS | package pin 49 | package pin 50 | High-speed OTG port; these are dedicated USB pins, not general GPIOs. Select with `ESP_USB_HOST_PORT_HIGH_SPEED`. |

ESP32-P4 has two full-speed USB PHY pin pairs, and the FS OTG and USB Serial/JTAG functions can choose between GPIO24/GPIO25 and GPIO26/GPIO27. The table above shows the typical/default assignment, not a guarantee for every board.

The FS USB pin-pair selection can be changed with eFuse, but this is irreversible once swapped and is not recommended for normal library use. Prefer using the board's default wiring and select the host peripheral in software with `EspUsbHostConfig::port`.

Only the OTG ports are usable as USB Host ports. Some boards make it hard to tell which connector is FS OTG versus a CDC/device connector, so check the board schematic and examples carefully.

The ESP-IDF USB Host stack can use only one host peripheral at a time. On ESP32-P4, choose either FS OTG or HS OTG with `EspUsbHostConfig::port`; you cannot run both as USB Host simultaneously. In Arduino-library use, the USB device function uses HS, and FS cannot be selected for that device role.

High-speed OTG support is still limited in practice. USB hubs are effectively not usable on HS OTG in the current environment. This library has confirmed a USB keyboard working as USB Host on ESP32-P4, but detailed ESP32-P4 validation is still incomplete.

## Installation

Open the Arduino IDE Library Manager, search for **EspUsbHost**, and install.

Or clone this repository into your Arduino `libraries/` folder:

```sh
git clone https://github.com/tanakamasayuki/EspUsbHost
```

## Quick start

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

## Examples

### HID

| Sketch | Description |
|--------|-------------|
| [EspUsbHostKeyboard](examples/HID/EspUsbHostKeyboard/) | Read keyboard input and print typed characters to Serial |
| [EspUsbHostKeyboardDump](examples/HID/EspUsbHostKeyboardDump/) | Dump parsed keyboard events and show how to handle `onKeyboard` yourself |
| [EspUsbHostMouse](examples/HID/EspUsbHostMouse/) | Read mouse movement and button events |
| [EspUsbHostConsumerControl](examples/HID/EspUsbHostConsumerControl/) | Detect media keys (volume, play/pause, etc.) |
| [EspUsbHostSystemControl](examples/HID/EspUsbHostSystemControl/) | Detect system keys (power, standby, wake) |
| [EspUsbHostGamepad](examples/HID/EspUsbHostGamepad/) | Read gamepad axes, hat switch, and buttons |
| [EspUsbHostHIDVendor](examples/HID/EspUsbHostHIDVendor/) | Vendor HID input and output/feature reports |
| [EspUsbHostHIDRawDump](examples/HID/EspUsbHostHIDRawDump/) | Raw hex dump with device address (supports multiple devices) |

### Info

| Sketch | Description |
|--------|-------------|
| [EspUsbHostDeviceInfo](examples/Info/EspUsbHostDeviceInfo/) | Print device descriptors, interfaces, and endpoints for all connected devices |
| [EspUsbHostHIDReportDescriptor](examples/Info/EspUsbHostHIDReportDescriptor/) | Print HID report descriptors and a simple item decode for HID investigation |
| [EspUsbHostCustomDeviceCallbacks](examples/Info/EspUsbHostCustomDeviceCallbacks/) | Define custom connect/disconnect callbacks and inspect connected devices |
| [EspUsbHostHubPPPS](examples/Info/EspUsbHostHubPPPS/) | Control power on a PPPS-capable USB hub port |

### MIDI

| Sketch | Description |
|--------|-------------|
| [EspUsbHostMIDI](examples/MIDI/EspUsbHostMIDI/) | USB MIDI input and output |

### Audio

| Sketch | Description |
|--------|-------------|
| [EspUsbHostAudioInput](examples/Audio/EspUsbHostAudioInput/) | Receive USB Audio isochronous IN payloads |
| [EspUsbHostAudioOutputTone](examples/Audio/EspUsbHostAudioOutputTone/) | Generate a simple tone and send it to USB Audio OUT |
| [EspUsbHostAudioOutputMP3PCMFlow](examples/Audio/EspUsbHostAudioOutputMP3PCMFlow/) | Decode embedded MP3 assets with PCMFlow and play them to USB Audio OUT |
| [EspUsbHostAudioOutputMP3ESP8266Audio](examples/Audio/EspUsbHostAudioOutputMP3ESP8266Audio/) | Decode embedded MP3 assets with ESP8266Audio and play them to USB Audio OUT |

### Serial

| Sketch | Description |
|--------|-------------|
| [EspUsbHostUSBSerial](examples/Serial/EspUsbHostUSBSerial/) | Bidirectional serial bridge (CDC ACM and VCP) |
| [EspUsbHostMultiUSBSerial](examples/Serial/EspUsbHostMultiUSBSerial/) | Use FTDI and CP210x USB serial devices at the same time |

### Storage

| Sketch | Description |
|--------|-------------|
| [EspUsbHostMSCBlockDump](examples/Storage/EspUsbHostMSCBlockDump/) | Print MSC capacity and dump the first block |

## API reference

### Core

```cpp
bool begin();
bool begin(const EspUsbHostConfig &config);
void end();
bool ready() const;
```

`EspUsbHostConfig` lets you adjust the background task stack size, priority, and core affinity:

```cpp
struct EspUsbHostConfig {
  uint32_t    taskStackSize = 4096;
  UBaseType_t taskPriority  = 5;
  BaseType_t  taskCore      = tskNO_AFFINITY;
  EspUsbHostPort port       = ESP_USB_HOST_PORT_DEFAULT;
};
```

On ESP32-P4, set `port` to `ESP_USB_HOST_PORT_FULL_SPEED` or `ESP_USB_HOST_PORT_HIGH_SPEED` when you need to choose a specific OTG peripheral. Other chips ignore this setting.

### Device events

```cpp
void onDeviceConnected(DeviceCallback callback);
void onDeviceDisconnected(DeviceCallback callback);
void espUsbHostPrint(const EspUsbHostDeviceInfo &device, Print &out = Serial);
```

Callbacks receive `const EspUsbHostDeviceInfo &device`. Key fields: `address`, `vid`, `pid`, `product`, `manufacturer`, `serial`, `speed`, `parentAddress`, `portId`.
Use `espUsbHostPrint(device)` for a one-line summary. Add event context such as `connected:` or `disconnected:` in your callback.

`portId` identifies where the device is attached. `0x01` means the root port. For hub-attached devices, the upper nibble is the hub index assigned in detection order and the lower nibble is the hub port number, for example `0x12` means hub #1 port 2.

### HID input

```cpp
void onKeyboard(KeyboardCallback callback);
void onMouse(MouseCallback callback);
void onConsumerControl(ConsumerControlCallback callback);
void onSystemControl(SystemControlCallback callback);
void onGamepad(GamepadCallback callback);
void onHIDInput(HIDInputCallback callback);    // raw — fires for all HID interfaces
void onVendorInput(VendorInputCallback callback);
void espUsbHostPrint(const EspUsbHostHIDInput &input, Print &out = Serial);
void espUsbHostPrint(const EspUsbHostKeyboardEvent &event, Print &out = Serial);
```

Notable event fields:

Parsed HID callbacks (`onKeyboard`, `onMouse`, `onConsumerControl`, `onSystemControl`, `onGamepad`, `onVendorInput`) all include `vid`, `pid`, `manufacturer`, `product`, `serial`, `rawData` / `rawLength` for the full input report, and `reportData` / `reportLength` for the report bytes after removing the Report ID when one is present.

| Callback | Key fields |
|----------|-----------|
| `onKeyboard` | `pressed`, `keycode`, `ascii`, `modifiers`, `address` |
| `onMouse` | `x`, `y`, `wheel`, `buttons`, `previousButtons`, `moved`, `buttonsChanged`, `address` |
| `onConsumerControl` | `pressed`, `usage` (16-bit HID usage code), `address` |
| `onSystemControl` | `pressed`, `usage` (8-bit), `address` |
| `onGamepad` | `fields`, `fieldCount`, `rawData`, `reportData`, `vid`, `pid`, `address` |
| `onHIDInput` | `address`, `vid`, `pid`, `interfaceNumber`, `subclass`, `protocol`, `data`, `length` |

### HID output

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

The default layout is `ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US`. Pass any of the following constants to `setKeyboardLayout()`:

| Constant | Locale | Notes |
|----------|--------|-------|
| `ESP_USB_HOST_KEYBOARD_LAYOUT_ZH_TW` | zh_TW | Traditional Chinese — US QWERTY symbols |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_DA_DK` | da_DK | Danish |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_DE_DE` | de_DE | German QWERTZ |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_EN_US` | en_US | English US (**default**) |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_FI_FI` | fi_FI | Finnish |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_FR_FR` | fr_FR | French AZERTY |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_HU_HU` | hu_HU | Hungarian QWERTZ |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_IT_IT` | it_IT | Italian |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_JA_JP` | ja_JP | Japanese |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_KO_KR` | ko_KR | Korean — US QWERTY symbols |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_NL_NL` | nl_NL | Dutch |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_NB_NO` | nb_NO | Norwegian Bokmål |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_PT_BR` | pt_BR | Brazilian Portuguese ABNT2 |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_SV_SE` | sv_SE | Swedish |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_ZH_CN` | zh_CN | Simplified Chinese — US QWERTY symbols |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_EN_GB` | en_GB | English UK |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_PT_PT` | pt_PT | Portuguese (Portugal) |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_ES_ES` | es_ES | Spanish |
| `ESP_USB_HOST_KEYBOARD_LAYOUT_FR_CH` | fr_CH | Swiss French QWERTZ |

`event.ascii` is a Latin-1 encoded `uint8_t` (0x00–0xFF). Dead keys (´, \`, ^, ~, ¨) and characters outside Latin-1 produce `ascii = 0`. For `ZH_TW`, `KO_KR`, and `ZH_CN`, the symbol layout is identical to `EN_US`; actual CJK character input requires an IME on the host side.

### USB serial (CDC ACM and VCP)

Low-level send API on `EspUsbHost`:

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

`EspUsbHostCdcSerial` wraps the above as a standard Arduino `Stream` / `Print`:

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

`EspUsbHostSerialConfig` defaults to 115200 8N1. `dataBits` supports 5 to 8 bits. `parity` accepts `ESP_USB_HOST_SERIAL_PARITY_NONE`, `ODD`, `EVEN`, `MARK`, or `SPACE`. `stopBits` accepts `ESP_USB_HOST_SERIAL_STOP_BITS_1`, `1_5`, or `2`.

Use `setAddress()` inside `onDeviceConnected` to bind a specific device when multiple USB serial devices are connected.

### MIDI

```cpp
void onMidiMessage(MidiCallback callback);   // receive

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

`onMidiMessage` callback receives `const EspUsbHostMidiMessage &message` with fields `cable`, `codeIndex`, `status`, `data1`, `data2`.

### USB audio

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

`onAudioData` receives raw payload bytes from USB Audio streaming isochronous IN endpoints. The callback receives `const EspUsbHostAudioData &audio` with fields `address`, `interfaceNumber`, `data`, and `length`.

`onAudioOutputRequest` is the preferred USB Audio OUT API. After `audioOutputStart()`, the library drives isochronous OUT transfers and calls the callback when it needs the next PCM frames. Fill `request.data` with up to `request.frameCount` interleaved frames and set `request.writtenFrames`; any unwritten frames are sent as silence and counted as underruns. The callback runs on the USB client task, so keep it short and non-blocking; copy from an existing PCM buffer rather than doing heavy decoding in the callback.

`getAudioStreams` reports the streaming endpoint direction, endpoint packet size, and UAC1 Type I format fields when available, including discrete sample rates or a continuous sample-rate range. `espUsbHostSelectAudioInputStream` and `espUsbHostSelectAudioOutputStream` apply an optional `(sampleRate, channels, bitsPerSample)` filter, then score the remaining candidates. The default scoring prefers 48 kHz, then 44.1 kHz, 16-bit PCM, and stereo when available. `setAudioSampleRate` sets the UAC1 sampling frequency request used when activating audio streaming endpoints. `audioSend` remains as a low-level API for manually submitting raw PCM payload bytes to a USB Audio streaming isochronous OUT endpoint.

`getAudioFeatureUnits` reports parsed UAC1 Audio Control Feature Units. `audioGetMute`, `audioSetMute`, `audioGetVolume`, `audioSetVolume`, and the dB/range helpers use UAC1 class-specific Feature Unit requests. `audioSetVolumeDbClamped` applies the device min/max/resolution when the range is available. `audioConfigureVolume` is the simple playback helper: it unmutes/mutes when mute is supported and sets clamped dB volume when volume is supported. The percent helpers treat `1..100` as a PCM amplitude ratio (`20 * log10(percent / 100)`) and round to the device step after clamping to min/max; `0` mutes when mute is supported, or falls back to minimum volume. `unitId=0` selects the first Feature Unit that exposes the requested control. `channel=0` means master; channel values starting at 1 address per-channel controls. Raw volume values are signed 1/256 dB units.

### USB Mass Storage

```cpp
bool mscReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool mscInquiry(EspUsbHostMscInquiry &inquiry,
                uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscRequestSense(EspUsbHostMscSense &sense,
                     uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
bool mscTestUnitReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS,
                      uint32_t timeoutMs = ESP_USB_HOST_MSC_DEFAULT_TIMEOUT_MS);
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
```

MSC support is currently block-level only. It supports SCSI transparent / Bulk-Only Transport devices and does not mount FAT or expose an Arduino `FS` object yet. Do not call these APIs from USB callbacks, because they wait for USB transfer completion.

### Device discovery

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
size_t maxEndpointChannelCount() const;
void   espUsbHostPrint(const EspUsbHostInterfaceInfo &interface,
                       Print &out = Serial);
void   espUsbHostPrint(const EspUsbHostEndpointInfo &endpoint,
                       Print &out = Serial);
void   printDeviceInfo(uint8_t address, bool includeHubInfo = false,
                       Print &out = Serial);
void   printAllDeviceInfo(Print &out = Serial);
```

Array size constants: `ESP_USB_HOST_MAX_DEVICES`, `ESP_USB_HOST_MAX_INTERFACES`, `ESP_USB_HOST_MAX_ENDPOINTS`.
`endpointChannelCount()` is an estimate based on endpoints in successfully claimed interfaces. `managedEndpointCount()` counts endpoints with persistent receive transfers managed by this library.

### Error handling

```cpp
int         lastError() const;
const char *lastErrorName() const;
```

## Design

**Callbacks over inheritance.** Register lambdas or functions with `onKeyboard()`, `onMouse()`, etc. The old pattern of subclassing `EspUsbHost` and overriding virtual methods is not the primary API.

**Breaking changes are accepted.** The library prioritises a clean Arduino-oriented API over backwards compatibility with its earlier inheritance-based interface.

**HID gamepad reports are exposed as mappable data.** `onGamepad()` reports descriptor-decoded fields plus raw/report bytes. It does not assign semantic names such as left stick X/Y or right stick X/Y because those fields vary by device and may be 8-bit, 12-bit, 16-bit, or packed. Use `vid` / `pid`, `fields`, `rawData`, and `reportData` to build the mapping that matches your controller.

**Non-goals:**
- Fully automatic interpretation of all HID report descriptors from the first implementation
- Implementing all USB classes in the first release
- ESP-IDF HID Host Driver API compatibility
- Exposing all USB spec internals directly as Arduino APIs

## Multiple devices

For multi-device use, a self-powered USB 2.0 hub is recommended. Cheap bus-powered hubs may fail to enumerate devices or become unstable when devices draw current. Many USB 3.x hubs also expose more complex internal topology or behavior that is not a good starting point for this library.

Prefer hubs with up to 4 downstream ports. Hubs with many ports are often implemented internally as cascaded hubs, which increases topology depth and resource use. ESP32-S3 has only 8 USB host channels, and some USB devices consume multiple channels for their interfaces/endpoints, so multi-device setups can hit the channel limit quickly.

All send APIs and `EspUsbHostCdcSerial` default to `ESP_USB_HOST_ANY_ADDRESS`, which targets the first available device of the appropriate class. Pass an explicit `address` to target the currently connected device.

Device identification fields have different stability:

| Field | Use |
|-------|-----|
| `address` | Current USB address. Use it for API calls such as `setAddress()`, `sendSerial()`, `midiSend()`, or `setKeyboardLeds()`. It can change after unplug/replug. |
| `portId` | Physical/topology location. Use it to remember "the device on this port" across reconnects. |
| `vid` / `pid` | Device type/model identification. Use it to select supported chip families or products. |
| `manufacturer` / `product` | Human-readable USB string descriptors. Useful for logs and UI, but not guaranteed unique. |
| `serial` | USB serial-number string descriptor when the device provides one. It may be empty. Use it for per-unit identity when available. |

```cpp
usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
  if (device.vid == 0x0403) {
    CdcSerial.setAddress(device.address);
  }
});
```

## Tests

- [`tests/peer/`](tests/peer/) — two-board USB tests using an ESP32-S3 as the device peer
- [`tests/loopback/`](tests/loopback/) — single-board loopback tests
- [`tests/manual/`](tests/manual/) — manual tests for special hardware and human verification

See [tests/README.md](tests/README.md) for setup instructions.

## Release checklist

1. **Clean working tree** — confirm `git status` shows no uncommitted changes
2. **Update dependencies** — use the [vscode-arduino-cli-wrapper](https://marketplace.visualstudio.com/items?itemName=tanakamasayuki.vscode-arduino-cli-wrapper) _sketch.yaml Versions_ feature to check all `sketch.yaml` files for outdated board/library versions; update to the latest and re-run steps 3–5 if anything changed
3. **Build check** — use _Build Check_ in vscode-arduino-cli-wrapper; minimum: `examples/` with the `esp32s3` profile; add all profiles if the change touches ESP32-P4 support
4. **Automated tests** — all `peer/` or `loopback/` tests pass
5. **Manual tests** — run tests related to the change (check `tests/.pytest-results/state.json` for last-run timestamps); not mandatory but strongly recommended
6. **CHANGELOG** — verify the entry for this release is accurate and complete
7. **Documentation** — confirm that API reference, examples, and README reflect the change
8. **Release** — trigger the GitHub Actions release workflow (`workflow_dispatch`, default: patch bump). The workflow will:
   - Bump the version in `library.properties` (major / minor / patch selectable)
   - Commit and push the version bump to the default branch
   - Create a `release` branch with `tests/` removed
   - Build a `.zip` archive of the library
   - Extract release notes from `CHANGELOG.md`
   - Create a GitHub release with the archive and release notes
