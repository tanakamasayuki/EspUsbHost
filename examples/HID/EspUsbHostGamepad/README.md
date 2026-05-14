# EspUsbHostGamepad

> 日本語版: [README.ja.md](README.ja.md)

Reads axis, hat switch, and button input from a USB HID gamepad and prints them to the Serial monitor.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB HID gamepad or joystick

## What it does

- Normalizes axis values to the range −1.0 to 1.0 (raw axes are `int8_t`, range −128 to 127)
- Applies a deadzone of ±10 — values within this range are reported as 0.0
- Displays the hat switch direction as a compass label (N, NE, E, …)
- Shows button press and release deltas alongside the current bitmask

## Key APIs

- `usb.onGamepad(callback)` — fired on each gamepad report with `EspUsbHostGamepadEvent`
  - `event.x`, `event.y` — left stick axes (`int8_t`)
  - `event.z`, `event.rz` — right stick axes (or throttle/rudder)
  - `event.rx`, `event.ry` — additional axes
  - `event.hat` — hat switch direction (0–7 = N/NE/E/SE/S/SW/W/NW, other = center)
  - `event.buttons` — 32-bit button bitmask (current state)
  - `event.previousButtons` — previous button bitmask

## Expected Serial output

```
connected: vid=054c pid=09cc product=Wireless Controller
x= 0.000 y= 0.000 z= 0.000 rz= 0.000 hat=- buttons=0x00000000
x= 0.000 y= 0.000 z= 0.000 rz= 0.000 hat=- buttons=0x00000001 +0x1
x= 0.000 y= 0.000 z= 0.000 rz= 0.000 hat=N buttons=0x00000001
```

> **Note:** Axis range and button layout vary by device and HID report descriptor.
> Devices that do not conform to the boot gamepad report format may not be recognized correctly.
