# EspUsbHostCustomHID

> 日本語版: [README.ja.md](README.ja.md)

Dumps raw HID input reports as hex for any connected HID device.
Useful for reverse-engineering unknown HID devices or developing custom HID parsers.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- Any USB HID device (keyboard, mouse, gamepad, custom device, etc.)

## What it does

- Receives all HID input reports regardless of device type
- Prints interface number, subclass, protocol, byte length, and raw hex data for each report

## Key APIs

- `usb.onHIDInput(callback)` — fired on every HID input report with `EspUsbHostHIDInput`
  - `input.interfaceNumber` — HID interface number
  - `input.subclass` — HID subclass (0 = no subclass, 1 = boot interface)
  - `input.protocol` — HID protocol (0 = none, 1 = keyboard, 2 = mouse)
  - `input.length` — report byte length
  - `input.data` — raw report bytes

## Expected Serial output

```
connected: vid=045e pid=07a5 product=USB Keyboard
hid iface=0 subclass=1 protocol=1 len=8 data=0000000000000000
hid iface=0 subclass=1 protocol=1 len=8 data=0000040000000000
```

> **Note:** This example is similar to [EspUsbHostHIDRawDump](../EspUsbHostHIDRawDump/), which also prints the device address and formats hex with spaces.
