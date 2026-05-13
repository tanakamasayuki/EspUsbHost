# EspUsbHost

Arduino library for using ESP32 USB Host from sketches.

## Target board

- ESP32-S3
- Boards supported by Arduino-ESP32 USB Host

## Current status

This branch is being rewritten around the API described in `SPEC.ja.md`.
The first implementation target is HID and CDC ACM input/output with background USB processing.
Common USB serial VCP devices are detected experimentally and use the same serial stream path when bulk endpoints are available.

## Examples

- `EspUsbHostKeyboard`: HID boot keyboard input
- `EspUsbHostMouse`: HID boot mouse input
- `EspUsbHostKeyboardLeds`: HID boot keyboard LED output
- `EspUsbHostConsumerControl`: HID consumer control input
- `EspUsbHostSystemControl`: HID system control input
- `EspUsbHostGamepad`: HID gamepad input
- `EspUsbHostHIDVendor`: HID vendor input
- `EspUsbHostCustomHID`: generic/custom HID input report dump
- `EspUsbHostUSBSerial`: CDC ACM USB serial input/output
- `EspUsbHostMIDI`: USB MIDI input/output
- `EspUsbHostHIDRawDump`: raw HID input report dump
- `EspUsbHostDeviceInfo`: connected device, interface, and endpoint information

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
- `void onSerialData(SerialDataCallback callback)`
- `void onConsumerControl(ConsumerControlCallback callback)`
- `void onSystemControl(SystemControlCallback callback)`
- `void onGamepad(GamepadCallback callback)`
- `void onVendorInput(VendorInputCallback callback)`
- `void setKeyboardLayout(EspUsbHostKeyboardLayout layout)`
- `bool sendHIDReport(uint8_t interfaceNumber, uint8_t reportType, uint8_t reportId, const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS)`
- `bool sendSerial(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS)`
- `bool sendSerial(const char *text, uint8_t address = ESP_USB_HOST_ANY_ADDRESS)`
- `bool serialReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const`
- `bool setSerialBaudRate(uint32_t baud, uint8_t address = ESP_USB_HOST_ANY_ADDRESS)`
- `bool midiReady(uint8_t address = ESP_USB_HOST_ANY_ADDRESS) const`
- `bool midiSend(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS)`
- `bool midiSendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t address = ESP_USB_HOST_ANY_ADDRESS)`
- `bool midiSendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t address = ESP_USB_HOST_ANY_ADDRESS)`
- `bool midiSendControlChange(uint8_t channel, uint8_t control, uint8_t value, uint8_t address = ESP_USB_HOST_ANY_ADDRESS)`
- `bool midiSendProgramChange(uint8_t channel, uint8_t program, uint8_t address = ESP_USB_HOST_ANY_ADDRESS)`
- `bool midiSendPolyPressure(uint8_t channel, uint8_t note, uint8_t pressure, uint8_t address = ESP_USB_HOST_ANY_ADDRESS)`
- `bool midiSendChannelPressure(uint8_t channel, uint8_t pressure, uint8_t address = ESP_USB_HOST_ANY_ADDRESS)`
- `bool midiSendPitchBend(uint8_t channel, uint16_t value, uint8_t address = ESP_USB_HOST_ANY_ADDRESS)`
- `bool midiSendPitchBendSigned(uint8_t channel, int16_t value, uint8_t address = ESP_USB_HOST_ANY_ADDRESS)`
- `bool midiSendSysEx(const uint8_t *data, size_t length, uint8_t address = ESP_USB_HOST_ANY_ADDRESS)`
- `bool setKeyboardLeds(bool numLock, bool capsLock, bool scrollLock, uint8_t address = ESP_USB_HOST_ANY_ADDRESS)`
- `size_t deviceCount() const`
- `size_t getDevices(EspUsbHostDeviceInfo *devices, size_t maxDevices) const`
- `bool getDevice(uint8_t address, EspUsbHostDeviceInfo &device) const`
- `size_t getInterfaces(uint8_t address, EspUsbHostInterfaceInfo *interfaces, size_t maxInterfaces) const`
- `size_t getEndpoints(uint8_t address, EspUsbHostEndpointInfo *endpoints, size_t maxEndpoints) const`
- `int lastError() const`
- `const char *lastErrorName() const`

`EspUsbHostCdcSerial` implements the usual Arduino `Stream` / `Print` style API:

- `bool begin(uint32_t baud = 115200)`
- `void end()`
- `bool connected() const`
- `int available()`
- `int read()`
- `int peek()`
- `void flush()`
- `size_t write(uint8_t data)`
- `size_t write(const uint8_t *buffer, size_t size)`
- `bool setBaudRate(uint32_t baud)`
- `bool setDtr(bool enable)`
- `bool setRts(bool enable)`
- `void setAddress(uint8_t address)`
- `uint8_t address() const`
- `void clearAddress()`

## Tests

- `tests/peer`: two-board USB tests using an ESP32-S3 device peer
- `tests/loopback`: single-board loopback tests for supported boards
