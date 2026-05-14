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
- **Device discovery** — enumerate connected devices, interfaces, and endpoints
- **Multiple devices** — each callback and send API accepts an optional `address` parameter to target a specific device

## Roadmap

### USB class support

| Class | Status |
|-------|--------|
| HID — keyboard, mouse, gamepad, consumer control, system control, vendor | ✅ Done |
| USB serial — CDC ACM and VCP (FTDI, CP210x, CH34x) via `EspUsbHostCdcSerial` | ✅ Done |
| USB MIDI | ✅ Done |
| UAC — USB audio input/output | 🔲 Experimental |
| HUB — hub detection, port power control | 🔲 Planned |
| MSC — USB storage | 💭 Under consideration |
| UVC — USB camera | 💭 Under consideration |

### Other planned features

| Feature | Status |
|---------|--------|
| `onHIDReportDescriptor()` — HID report descriptor access | 🔲 Planned |
| Multi-CDC example — multiple `EspUsbHostCdcSerial` instances with `setAddress()` | 🔲 Planned |
| Loopback tests (ESP32-P4 single-board) | 🔲 In progress |
| Manual tests — VCP serial, multi-device, hot-plug | 🔲 In progress |

## Requirements

- ESP32-S3, or any board supported by Arduino-ESP32 USB Host
- Arduino-ESP32 core

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
| [EspUsbHostMouse](examples/HID/EspUsbHostMouse/) | Read mouse movement and button events |
| [EspUsbHostKeyboardLeds](examples/HID/EspUsbHostKeyboardLeds/) | Control NumLock / CapsLock / ScrollLock LEDs |
| [EspUsbHostConsumerControl](examples/HID/EspUsbHostConsumerControl/) | Detect media keys (volume, play/pause, etc.) |
| [EspUsbHostSystemControl](examples/HID/EspUsbHostSystemControl/) | Detect system keys (power, standby, wake) |
| [EspUsbHostGamepad](examples/HID/EspUsbHostGamepad/) | Read gamepad axes, hat switch, and buttons |
| [EspUsbHostHIDVendor](examples/HID/EspUsbHostHIDVendor/) | Vendor HID input and output/feature reports |
| [EspUsbHostCustomHID](examples/HID/EspUsbHostCustomHID/) | Raw hex dump of any HID input report |
| [EspUsbHostHIDRawDump](examples/HID/EspUsbHostHIDRawDump/) | Raw hex dump with device address (supports multiple devices) |

### Info

| Sketch | Description |
|--------|-------------|
| [EspUsbHostDeviceInfo](examples/Info/EspUsbHostDeviceInfo/) | Print device descriptors, interfaces, and endpoints for all connected devices |

### MIDI

| Sketch | Description |
|--------|-------------|
| [EspUsbHostMIDI](examples/MIDI/EspUsbHostMIDI/) | USB MIDI input and output |

### Audio

| Sketch | Description |
|--------|-------------|
| [EspUsbHostAudioInput](examples/Audio/EspUsbHostAudioInput/) | Receive USB Audio isochronous IN payloads |
| [EspUsbHostAudioOutput](examples/Audio/EspUsbHostAudioOutput/) | Send USB Audio isochronous OUT payloads |

### Serial

| Sketch | Description |
|--------|-------------|
| [EspUsbHostUSBSerial](examples/Serial/EspUsbHostUSBSerial/) | Bidirectional serial bridge (CDC ACM and VCP) |

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
};
```

### Device events

```cpp
void onDeviceConnected(DeviceCallback callback);
void onDeviceDisconnected(DeviceCallback callback);
```

Callbacks receive `const EspUsbHostDeviceInfo &device`. Key fields: `address`, `vid`, `pid`, `product`, `manufacturer`, `serial`, `speed`, `parentAddress`, `portId`.

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
```

Notable event fields:

| Callback | Key fields |
|----------|-----------|
| `onKeyboard` | `pressed`, `keyCode`, `ascii`, `modifiers`, `address` |
| `onMouse` | `x`, `y`, `wheel`, `buttons`, `previousButtons`, `moved`, `buttonsChanged`, `address` |
| `onConsumerControl` | `pressed`, `usage` (16-bit HID usage code), `address` |
| `onSystemControl` | `pressed`, `usage` (8-bit), `address` |
| `onGamepad` | `x`, `y`, `z`, `rz`, `rx`, `ry`, `hat`, `buttons`, `previousButtons`, `address` |
| `onHIDInput` / `onVendorInput` | `address`, `interfaceNumber`, `subclass`, `protocol`, `data`, `length` |

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

Keyboard layout constants: `ESP_USB_HOST_KEYBOARD_LAYOUT_US`, `ESP_USB_HOST_KEYBOARD_LAYOUT_JP`.

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
bool    setDtr(bool enable);
bool    setRts(bool enable);
void    setAddress(uint8_t address);
uint8_t address() const;
void    clearAddress();
```

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
bool audioReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool audioOutputReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const;
bool setAudioSampleRate(uint32_t sampleRate,
                        uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
bool audioSend(const uint8_t *data, size_t length,
               uint8_t address = ESP_USB_HOST_ANY_ADDRESS);
```

`onAudioData` receives raw payload bytes from USB Audio streaming isochronous IN endpoints. The callback receives `const EspUsbHostAudioData &audio` with fields `address`, `interfaceNumber`, `data`, and `length`.

`setAudioSampleRate` sets the UAC1 sampling frequency request used when activating audio streaming endpoints. `audioSend` writes raw PCM payload bytes to a USB Audio streaming isochronous OUT endpoint. The library does not parse format descriptors yet, so the caller must match the device's sample rate, channel count, and sample size.

### Device discovery

```cpp
size_t deviceCount() const;
size_t getDevices(EspUsbHostDeviceInfo *devices, size_t maxDevices) const;
bool   getDevice(uint8_t address, EspUsbHostDeviceInfo &device) const;
size_t getInterfaces(uint8_t address, EspUsbHostInterfaceInfo *interfaces,
                     size_t maxInterfaces) const;
size_t getEndpoints(uint8_t address, EspUsbHostEndpointInfo *endpoints,
                    size_t maxEndpoints) const;
```

Array size constants: `ESP_USB_HOST_MAX_DEVICES`, `ESP_USB_HOST_MAX_INTERFACES`, `ESP_USB_HOST_MAX_ENDPOINTS`.

### Error handling

```cpp
int         lastError() const;
const char *lastErrorName() const;
```

## Design

**Callbacks over inheritance.** Register lambdas or functions with `onKeyboard()`, `onMouse()`, etc. The old pattern of subclassing `EspUsbHost` and overriding virtual methods is not the primary API.

**Breaking changes are accepted.** The library prioritises a clean Arduino-oriented API over backwards compatibility with its earlier inheritance-based interface.

**Non-goals:**
- Fully automatic interpretation of all HID report descriptors
- Implementing all USB classes in the first release
- ESP-IDF HID Host Driver API compatibility
- Exposing all USB spec internals directly as Arduino APIs

## Multiple devices

All send APIs and `EspUsbHostCdcSerial` default to `ESP_USB_HOST_ANY_ADDRESS`, which targets the first available device of the appropriate class. Pass an explicit `address` (obtained from `EspUsbHostDeviceInfo::address`) to target a specific device.

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
