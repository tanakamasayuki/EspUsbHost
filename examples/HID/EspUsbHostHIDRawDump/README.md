# EspUsbHostHIDRawDump

> 日本語版: [README.ja.md](README.ja.md)

Dumps raw HID input reports from all connected devices with device address, interface, and protocol information.
Supports multiple simultaneous USB devices.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- One or more USB HID devices (keyboard, mouse, gamepad, etc.)

## What it does

- Prints device address, VID, PID, and product name on connect/disconnect
- Dumps raw HID input reports as space-separated hex with address and interface metadata

## Key APIs

- `usb.onHIDInput(callback)` — fired on every HID input report with `EspUsbHostHIDInput`
  - `input.address` — USB device address (distinguishes multiple devices)
  - `input.interfaceNumber` — HID interface number
  - `input.subclass` — HID subclass (0 = no subclass, 1 = boot interface)
  - `input.protocol` — HID protocol (0 = none, 1 = keyboard, 2 = mouse)
  - `input.length` — report byte length
  - `input.data` — raw report bytes
- `usb.onDeviceConnected(callback)` / `usb.onDeviceDisconnected(callback)`
  - `device.address` — USB device address

## Expected Serial output

```
connected: address=1 vid=045e pid=07a5 product=USB Keyboard
hid: address=1 iface=0 subclass=0x01 protocol=0x01 len=8 data=00 00 00 00 00 00 00 00
hid: address=1 iface=0 subclass=0x01 protocol=0x01 len=8 data=00 00 04 00 00 00 00 00
disconnected: address=1 vid=045e pid=07a5
```

## See also

- [EspUsbHostCustomHID](../EspUsbHostCustomHID/) — similar dump without address info
- [EspUsbHostDeviceInfo](../../Info/EspUsbHostDeviceInfo/) — full device/interface/endpoint enumeration
