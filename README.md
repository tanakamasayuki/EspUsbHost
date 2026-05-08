# EspUsbHost

Arduino library for using ESP32 USB Host from sketches.

## Target board

- ESP32-S3
- Boards supported by Arduino-ESP32 USB Host

## Current status

This branch is being rewritten around the API described in `SPEC.ja.md`.
The first implementation target is HID keyboard, mouse, consumer control, and gamepad input with background USB processing.

## Examples

- `EspUsbHostKeybord`: HID boot keyboard input
- `EspUsbHostMouse`: HID boot mouse input
- `EspUsbHostKeyboardLeds`: HID boot keyboard LED output
- `EspUsbHostConsumerControl`: HID consumer control input
- `EspUsbHostGamepad`: HID gamepad input
- `EspUsbHostHIDVendor`: HID vendor input
- `EspUsbHostHIDRawDump`: raw HID input report dump
- `EspUsbHostHubProbe`: USB Host/Hub enumeration probe

## Keyboard example

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

## Implemented API

- `bool begin()`
- `bool begin(const EspUsbHostConfig &config)`
- `void end()`
- `bool ready() const`
- `void onDeviceConnected(DeviceCallback callback)`
- `void onDeviceDisconnected(DeviceCallback callback)`
- `void onKeyboard(KeyboardCallback callback)`
- `void onMouse(MouseCallback callback)`
- `void onHIDInput(HIDInputCallback callback)`
- `void onConsumerControl(ConsumerControlCallback callback)`
- `void onGamepad(GamepadCallback callback)`
- `void onVendorInput(VendorInputCallback callback)`
- `void setKeyboardLayout(EspUsbHostKeyboardLayout layout)`
- `bool sendHIDReport(uint8_t interfaceNumber, uint8_t reportType, uint8_t reportId, const uint8_t *data, size_t length)`
- `bool setKeyboardLeds(bool numLock, bool capsLock, bool scrollLock)`
- `int lastError() const`
- `const char *lastErrorName() const`

## Tests

- `tests/standalone/host_logic`: USB-independent HID logic tests
- `tests/peer`: two-board USB HID tests using an ESP32-S3 device peer
