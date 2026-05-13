# EspUsbHostGamepad

> 日本語版: [README.ja.md](README.ja.md)

Reads axis, hat switch, and button input from a USB HID gamepad and prints them to the Serial monitor.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB HID gamepad or joystick

## What it does

- Prints all gamepad event fields whenever a report is received from the device

## Key APIs

- `usb.onGamepad(callback)` — fired on each gamepad report with `EspUsbHostGamepadEvent`
  - `event.x`, `event.y` — left stick axes
  - `event.z`, `event.rz` — right stick axes (or throttle/rudder)
  - `event.rx`, `event.ry` — additional axes
  - `event.hat` — hat switch direction (0–8, 0 = center)
  - `event.buttons` — 32-bit button bitmask (current state)
  - `event.previousButtons` — previous button bitmask

## Expected Serial output

```
connected: vid=054c pid=09cc product=Wireless Controller
gamepad x=0 y=0 z=128 rz=128 rx=0 ry=0 hat=0 buttons=0x00000000 previous=0x00000000
gamepad x=0 y=0 z=128 rz=128 rx=0 ry=0 hat=0 buttons=0x00000001 previous=0x00000000
```

> **Note:** Axis range and button layout vary by device and HID report descriptor.
> Devices that do not conform to the boot gamepad report format may not be recognized correctly.
