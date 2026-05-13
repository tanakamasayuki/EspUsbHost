# EspUsbHost Design Specification

> 日本語版: [SPEC.ja.md](SPEC.ja.md)

This document summarizes the user-facing design policy for a full rewrite of EspUsbHost.
It does not cover internal implementation details, ESP-IDF-derived structures, or how class drivers are split internally.

This file is intended to share requirements, design policies, user experience goals, scope, and constraints.
API names, struct names, field names, and code examples in this document are drafts used to illustrate policy — they are not final specifications.
Actual names, type definitions, arguments, and class structure will be finalized during the implementation phase as they are reconciled with the code.

## Core policy

The new EspUsbHost prioritizes a clean, simple Arduino-oriented API over backwards compatibility with the existing API.

The current library has accumulated legacy assumptions in its internal structure and public API through a history of porting and extending old code for Arduino.
This rewrite accepts breaking changes in order to clean up what users see.

The following policies are prioritized:

- No need to call a USB processing function from `loop()`
- Use callback registration and Arduino-standard `Stream` / `Print` compatibility rather than inheritance-based overrides
- Treat HUB, CDC, MSC, UAC, UVC, USB MIDI, and others with the same model rather than being HID-only
- Keep low-level information and debug logs while making the everyday API short and clear
- Avoid complexity introduced only for old API compatibility

## Purpose

EspUsbHost is a library for easily using ESP32 USB Host functionality from Arduino.

The priority is to let Arduino users handle HID input, serial communication, storage, audio, video, MIDI, and more with short sketches — without needing to understand USB internals.

The design emphasizes:

- Simplicity from the Arduino sketch perspective
- Fitting naturally into `setup()` and `loop()`
- Callback registration as the default over inheritance
- High-level APIs for commonly used USB classes
- Access to class-specific raw data and descriptors when needed
- Built-in support for adding USB classes beyond HID
- Accepting breaking changes without being constrained by old APIs

## Basic pattern

The basic user pattern is to create a global `EspUsbHost`, register the needed callbacks in `setup()`, and call `begin()`.
The library creates a background FreeRTOS task to process USB events, so ordinary sketches do not need to call any USB processing function from `loop()`.

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

Using inheritance to override virtual functions is not a primary API pattern in the new design.

## Public API philosophy

The public API is limited to what Arduino users will naturally use.

Minimal core API:

```cpp
bool begin();
bool begin(const EspUsbHostConfig &config);
void end();
bool ready() const;
```

Task configuration:

```cpp
struct EspUsbHostConfig {
  uint32_t taskStackSize = 4096;
  UBaseType_t taskPriority = 5;
  BaseType_t taskCore = tskNO_AFFINITY;
};
```

Event registration API:

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

`begin()` returns `true` on successful initialization.
`begin()` initializes the USB Host and starts the background task.
`end()` shuts down USB Host and releases allocated resources.
`ready()` returns whether USB Host is in a usable state.

## USB class support policy

EspUsbHost is a USB Host library that handles multiple USB classes with the same model, not just HID.

Each class's API exposes only the operations Arduino users interact with most frequently at the high level.
For fine-grained control, class-specific raw events and low-level send/receive APIs fill the gap.

USB classes in scope:

| USB class | Purpose | API policy |
|-----------|---------|------------|
| HID | Keyboard, mouse, gamepad, barcode reader, etc. | `onKeyboard()`, `onMouse()`, `onGamepad()`, `onHIDInput()` |
| CDC ACM | USB serial, modem devices | `EspUsbHostCdcSerial`, `onCdcData()` |
| MSC | USB storage, card readers | mount/unmount events, filesystem integration |
| UAC | USB audio | audio input/output streams |
| UVC | USB camera | frame receive events |
| USB MIDI | MIDI keyboard, controller, sound module | `onMidiMessage()`, `midiSend()` |
| HUB | USB hub, multiple device connection, port power management | device path, port events, port power control |

CDC, MSC, UAC, and UVC have host implementations or samples on the ESP-IDF or esp-usb side, which will inform behavior verification and API design.
USB MIDI is treated like CDC — a peripheral that sends and receives data over endpoints — and the API prioritizes MIDI-message-level access from Arduino.
HUB is treated as the foundation for multiple-device support, and for capable hubs, per-port power control via Port Power Switching is also provided.

Regardless of which class is added, the basic user pattern remains unified: `EspUsbHost usb;`, `usb.onXxx(...)`, `usb.begin()`.

## Implementation priority

Implementation proceeds based on frequency of use, ease of building an Arduino API, and availability of existing samples or drivers.

Recommended order:

1. HID redesign
2. HUB basic support
3. CDC ACM / VCP
4. USB MIDI
5. MSC
6. UAC
7. UVC

HID is handled first as a replacement for the current functionality.
HUB provides the foundation for multiple device support and port power management, so it comes after HID.
CDC is easy to wrap as a Serial-style API and easy to connect to existing Arduino sketches and libraries, so it is prioritized early.
USB MIDI has no existing host driver but can start small around bulk transfers, so it follows CDC.
MSC requires VFS/FAT integration, and UAC and UVC have large buffer, bandwidth, and memory constraints, so they come later.

## Device events

USB device connect and disconnect events are available to users who need them.

User callbacks are called from the library's USB event processing task.
Short `Serial.print()` calls inside callbacks are acceptable, but avoid long-blocking operations, heavy file I/O, or long delays.
When needed, pass data to a flag or queue inside the callback and do heavy work in the user's `loop()` or a separate task.

```cpp
usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
  Serial.printf("connected: vid=%04x pid=%04x\n", device.vid, device.pid);
});

usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
  Serial.println("disconnected");
});
```

`EspUsbHostDeviceInfo` includes only the information users need for logging and branching.

Planned fields:

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

If string information cannot be retrieved, it will be an empty string.

## HUB

USB Hub support allows multiple devices to be connected simultaneously.
For devices connected via a hub, the basic user API is the same as for directly connected devices.

Hub support allows devices to be identified not only by USB address but also by connection position including hub and port.

Planned fields:

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

An API will be provided to receive hub port state changes.

```cpp
usb.onHubPortChanged([](const EspUsbHostHubPortEvent &event) {
  Serial.printf("hub=%u port=%u connected=%d powered=%d\n",
                event.hubAddress,
                event.portNumber,
                event.connected,
                event.powered);
});
```

For hubs that support Port Power Switching, per-port power on/off will be available.
This can be used for logical device disconnection, reconnection, and recovery.

```cpp
usb.setHubPortPower(hubAddress, portNumber, false);
delay(500);
usb.setHubPortPower(hubAddress, portNumber, true);
```

Planned API:

```cpp
bool setHubPortPower(uint8_t hubAddress, uint8_t portNumber, bool enable);
bool resetHubPort(uint8_t hubAddress, uint8_t portNumber);
```

Port Power Switching requires hub-side support.
For hubs that do not support per-port power control in their hub descriptor, the API returns `false` and emits a warning log.

Hub support will document endpoint count, device count, power supply, and hub depth constraints.
Initial support prioritizes single-level hubs; multi-level hubs will be handled incrementally based on USB Host stack and memory/endpoint constraints.

## Keyboard

Keyboard input is prioritized as a per-key event.

```cpp
usb.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
  if (event.pressed) {
    Serial.printf("key=%02x ascii=%c\n", event.keycode, event.ascii);
  }
});
```

Planned fields:

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

`pressed` is true at the moment a key is pressed; `released` is true at the moment it is released.
`ascii` holds a value only when conversion is possible; it is `0` for keys that cannot be converted.

Simultaneous key presses, modifier keys, and key repeat are provided as events that Arduino users can understand.

The keyboard layout is configurable:

```cpp
usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_US);
usb.setKeyboardLayout(ESP_USB_HOST_KEYBOARD_LAYOUT_JP);
```

The default layout is US.

## Mouse

Mouse events deliver movement, wheel, and button state together.

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

Planned fields:

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

Button constants will be provided with readable names:

```cpp
ESP_USB_HOST_MOUSE_LEFT
ESP_USB_HOST_MOUSE_RIGHT
ESP_USB_HOST_MOUSE_MIDDLE
ESP_USB_HOST_MOUSE_BACK
ESP_USB_HOST_MOUSE_FORWARD
```

## Gamepad

Gamepad is a primary target for HID extension.
However, because device-to-device variation is large, fully normalizing all gamepads from the start is not a goal.

The high-level API aims to handle common buttons, sticks, and triggers.

```cpp
usb.onGamepad([](const EspUsbHostGamepadEvent &event) {
  Serial.printf("lx=%d ly=%d buttons=%08x\n",
                event.leftX, event.leftY, event.buttons);
});
```

It can be combined with the raw HID API so users can inspect reports for unsupported devices.

## Raw HID

A raw input report API is provided for HID extension and debugging.

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

Planned fields:

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

`data` is only valid for the duration of the callback.
Copy it yourself if you need to use it later.

## CDC

CDC ACM provides a high-level API for use as USB serial.
The primary API is `EspUsbHostCdcSerial`, which is `Stream` / `Print` compatible and prioritizes use similar to Arduino's `Serial`.

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

`EspUsbHostCdcSerial` inherits from `Stream`, so the goal is for it to be passable to existing code that accepts `Stream &` or `Print &`.

```cpp
void bridge(Stream &from, Print &to) {
  while (from.available()) {
    to.write(from.read());
  }
}

bridge(Serial, CdcSerial);
bridge(CdcSerial, Serial);
```

Planned API:

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

Full compatibility with APIs fixed to `HardwareSerial &` is not a goal.
The priority is compatibility with libraries that accept `Stream &` or `Print &` in Arduino.

An event-style API is also available:

```cpp
usb.onCdcData([](const EspUsbHostCdcData &data) {
  Serial.write(data.data, data.length);
});
```

Planned fields:

```cpp
struct EspUsbHostCdcData {
  uint8_t address;
  uint8_t interfaceNumber;
  const uint8_t *data;
  size_t length;
};
```

Simple send APIs may also be provided directly on `EspUsbHost`, but `EspUsbHostCdcSerial` is recommended for typical CDC use:

```cpp
size_t cdcWrite(const uint8_t *data, size_t length);
size_t cdcWrite(const char *text);
```

In anticipation of multiple CDC devices or multiple interfaces, interface-specific APIs will be provided in the future:

```cpp
size_t cdcWrite(uint8_t interfaceNumber, const uint8_t *data, size_t length);
```

## USB MIDI

USB MIDI prioritizes handling at the MIDI message level rather than raw bytes.

```cpp
usb.onMidiMessage([](const EspUsbHostMidiMessage &message) {
  Serial.printf("status=%02x data1=%02x data2=%02x\n",
                message.status, message.data1, message.data2);
});

usb.midiSendNoteOn(0, 60, 100);
usb.midiSendNoteOff(0, 60, 0);
```

Planned fields:

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

Basic send API:

```cpp
bool midiSend(const uint8_t *data, size_t length);
bool midiSendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
bool midiSendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);
bool midiSendControlChange(uint8_t channel, uint8_t control, uint8_t value);
```

## MSC

The goal of MSC is to allow Arduino to access USB storage.
Because how the filesystem connects depends on the board environment and what Arduino-ESP32 provides, the user-facing API centers on mount/unmount events.

```cpp
usb.onMscMounted([](const EspUsbHostMscVolume &volume) {
  Serial.println("storage mounted");
});

usb.onMscUnmounted([](const EspUsbHostMscVolume &volume) {
  Serial.println("storage removed");
});
```

MSC can be deferred behind other classes in the initial phase.
However, the API design should accommodate classes like storage that perform continuous I/O after connection.

## UAC

UAC is a candidate extension for handling USB audio input or output.

```cpp
usb.onAudioInput([](const EspUsbHostAudioBuffer &audio) {
  // PCM samples
});
```

Because sample rate, channel count, and sample format configuration is required, more configuration API than HID or CDC is acceptable.

## UVC

UVC is a candidate extension for handling USB camera frames.

```cpp
usb.onVideoFrame([](const EspUsbHostVideoFrame &frame) {
  Serial.printf("frame %ux%u len=%u\n",
                frame.width, frame.height, frame.length);
});
```

Due to ESP32 memory constraints, the initial spec assumes low resolution, MJPEG, and per-frame or chunked reception.
UVC will present a high-level API while clearly documenting constraints such as buffer size and resolution.

## HID Report Descriptor

An API is provided to notify users who need to inspect the HID report descriptor.

```cpp
usb.onHIDReportDescriptor([](const EspUsbHostHIDReportDescriptor &report) {
  Serial.printf("iface=%u len=%u\n", report.interfaceNumber, report.length);
});
```

Planned fields:

```cpp
struct EspUsbHostHIDReportDescriptor {
  uint8_t address;
  uint8_t interfaceNumber;
  const uint8_t *data;
  size_t length;
};
```

`data` is only valid for the duration of the callback.

## HID output

HID output is supported from the initial spec.
Keyboard LED control is lightweight and frequently used, so it is provided as a high-level API from the start.

```cpp
usb.setKeyboardLeds(true, false, false);  // NumLock, CapsLock, ScrollLock
```

For finer control, an API equivalent to the HID class request `Set_Report` is also provided:

```cpp
bool sendHIDReport(uint8_t interfaceNumber,
                   uint8_t reportType,
                   uint8_t reportId,
                   const uint8_t *data,
                   size_t length);
```

`sendHIDReport()` is used to send Output and Feature reports.
`reportType` specifies the HID report type: Input, Output, or Feature.
For typical keyboard LED control, users use `setKeyboardLeds()` rather than calling `sendHIDReport()` directly.

```cpp
uint8_t leds = ESP_USB_HOST_KEYBOARD_LED_NUM_LOCK |
               ESP_USB_HOST_KEYBOARD_LED_CAPS_LOCK;

usb.sendHIDReport(interfaceNumber,
                  ESP_USB_HOST_HID_REPORT_TYPE_OUTPUT,
                  0,
                  &leds,
                  1);
```

LED constants:

```cpp
ESP_USB_HOST_KEYBOARD_LED_NUM_LOCK
ESP_USB_HOST_KEYBOARD_LED_CAPS_LOCK
ESP_USB_HOST_KEYBOARD_LED_SCROLL_LOCK
ESP_USB_HOST_KEYBOARD_LED_COMPOSE
ESP_USB_HOST_KEYBOARD_LED_KANA
```

Initial support prioritizes Boot keyboard LED control.
For Report protocol keyboards or composite HID devices where full automatic interpretation based on the report descriptor is needed, the raw output API can fill the gap.

## Logging

Library debug information is output according to the Arduino-ESP32 log level.
Per-feature explicit on/off APIs are kept to a minimum; log levels are the primary control mechanism.

The existing implementation logs USB device, configuration, interface, endpoint, HID descriptors, and transfer state in considerable detail.
The new implementation will maintain this level of detail and not reduce the amount of information visible during debugging through feature additions or internal reorganization.

Log level guidelines:

| Level | Content |
|-------|---------|
| Error | Initialization failure, device open failure, unrecoverable transfer errors |
| Warn | Unsupported descriptors, buffer overflow, partial feature disabling |
| Info | Device connect/disconnect, VID/PID, interface summary, mount/open/close |
| Debug | Descriptor details, class requests, state transitions |
| Verbose | Raw transfers, PCAP TEXT, detailed send/receive dumps |

Information maintained in logs:

- USB Host library/client install, uninstall, and event handling results
- Device connect/disconnect, device address, speed, VID/PID, string descriptors
- Key fields of device/configuration/interface/endpoint/class-specific descriptors
- Results of interface claim/release, endpoint clear/halt/flush, transfer alloc/free/submit
- Control transfer request/response summary
- Interrupt/bulk/isoc transfer endpoint, byte count, and status
- HID report descriptor retrieval result and HID item analysis where possible
- Per class-driver open/close/start/stop, mount/unmount, stream start/stop

Logs will maintain a human-readable format, and tool-convertible output such as PCAP TEXT will not break its format.

### PCAP TEXT

The existing feature that outputs USB send/receive content in PCAP-style text as `PCAP TEXT` dumps will be maintained.
`PCAP TEXT` is treated as a form of log output and emitted at Verbose log level.

This output is intended to be converted to pcap format with `text2pcap` and inspected in Wireshark.
By comparing it with pcap captured from a USB analyzer on real hardware, it can be used to verify and debug library behavior.

Output targets:

- Control transfers directly issued by the library
- Interrupt/bulk/isoc transfers directly issued by the library
- Content received in transfer callbacks
- USB request/response pairs the library is aware of, such as descriptor retrieval and HID report requests

PCAP TEXT outputs received data confirmed through callbacks and sent data issued by the library.
However, the library cannot fully observe sends, retransmissions, ACK/NAK, and low-level bus behavior issued internally by the USB Host stack or lower drivers.

Accordingly, the output includes information that distinguishes the type:

- `observed`: actually received by the library, e.g., through a transfer callback
- `submitted`: a send request the library submitted to the USB Host API
- `inferred`: recorded by the library based on inference from descriptor information or API calls

`inferred` is not a complete empirical measurement including USB Host stack internal processing.
When comparing with real-hardware pcap in Wireshark, keep this distinction in mind.

PCAP TEXT is a debug feature; with Verbose logging disabled during normal use, it will not be output.
Because the output processing is heavy, runtime performance with Verbose logging enabled is not guaranteed for UAC/UVC and other real-time-sensitive classes.

## Test policy

Testing distinguishes between USB Host behavior that requires real hardware and logic that can be verified without it.

In local environments, the basic approach is simple text-based tests that do not depend on a specific test framework such as Unity or ArduTest.
Test targets output results to stdout or Serial, and pytest checks the final `OK` / `NG` and necessary log lines.

Expected output:

```text
TEST_BEGIN host_logic
PASS hid_parser
PASS midi_packet
PASS cdc_buffer
TEST_END pass=3 fail=0
OK
```

On failure:

```text
TEST_BEGIN host_logic
PASS hid_parser
FAIL pcap_text_format expected=... actual=...
TEST_END pass=1 fail=1
NG
```

This approach allows the same pattern to be used for testing on `host`, ESP32 real hardware, and if needed an Arduino Uno, without increasing dependencies.

Verified locally:

- HID report parser
- HID descriptor/report helpers
- Keycode conversion
- Keyboard LED output report generation
- USB MIDI packet encode/decode
- CDC buffer/Stream compatibility logic
- PCAP TEXT formatter
- Descriptor parser
- State management and error handling

Verified on real hardware:

- USB Host initialization
- Device connect/disconnect
- HUB connection and port state changes
- Port power on/off via PPPS
- Communication with real HID/CDC/MIDI/MSC devices
- Resource release on disconnect/reconnect
- Log output and PCAP TEXT output

Tests requiring additional hardware such as HUBs and PPPS are run only in environments where they are feasible.
Where possible, connect multiple devices to a hub and automate plug/unplug-equivalent tests with port power on/off.

Unity and ArduTest are not required.
They are available as optional aids for tests that need them, but the basic policy prioritizes text-based `OK` / `NG` judgment.

## Error handling

For Arduino, the design avoids requiring users to handle detailed error codes in normal use.

Core APIs return `bool` for success/failure.
For users who want details, an API for retrieving the last error is provided:

```cpp
int lastError() const;
const char *lastErrorName() const;
```

Non-fatal transfer errors and temporary disconnect handling are recovered by the library where possible.

## Target scope

Initial targets:

- ESP32-S3
- Boards on which the Arduino-ESP32 USB Host API is available

Initial USB class support:

- HID Keyboard
- HID Mouse
- HID raw input
- HID output report
- Keyboard LED control

Extension candidates:

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

Even as classes beyond HID are added, the basic user pattern remains `begin()` and `onXxx()`.

## Relationship to the old API

The new spec accepts breaking changes.

The old inheritance-based virtual callback API is not included in the primary spec.

Old API example:

```cpp
class MyEspUsbHost : public EspUsbHost {
  void onKeyboardKey(uint8_t ascii, uint8_t keycode, uint8_t modifier) {
  }
};
```

Replaced in the new spec with:

```cpp
EspUsbHost usb;

usb.onKeyboard([](const EspUsbHostKeyboardEvent &event) {
});
```

A temporary compatibility layer or migration guide may be provided to help with migration, but the long-term primary API will be callback registration style.

## Example policy

Examples center on short sketches that Arduino users can run immediately.

Examples to provide:

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

Examples do not put USB descriptors or ESP-IDF concepts front and center.
Raw dump examples and class-specific dump examples are available for users who need to inspect that level of detail.

## Naming policy

Names that are readable in Arduino sketches are prioritized.

- Class name: `EspUsbHost`
- Struct names make the purpose explicit: e.g., `EspUsbHostKeyboardEvent`
- Method names are short: `onKeyboard()`, `onMouse()`, `onCdcData()`, `onMidiMessage()`
- Constants use the `ESP_USB_HOST_` prefix

ESP-IDF type names and detailed HID spec names are not exposed in the public API except where necessary.

## Non-goals

The following are not goals for the initial spec:

- Fully automatic interpretation of all HID devices
- Implementing all USB classes in the first release
- ESP-IDF HID Host Driver API compatibility
- Exposing all USB spec features directly as Arduino APIs
- Making descriptor parser details visible to users
- Recording all packets on the USB bus as a complete empirical pcap

## Summary

The new EspUsbHost centers on an event-driven API that is easy to use from Arduino sketches rather than an ESP-IDF-style component structure.

At the high level it provides `onKeyboard()`, `onMouse()`, `onGamepad()`, `onCdcData()`, `onMidiMessage()`, and others; for extension and debugging it provides per-class raw events and PCAP TEXT output.

The internals can be fully rewritten, and users are offered the simple pattern of "create, register callbacks, call begin, no task polling required."
