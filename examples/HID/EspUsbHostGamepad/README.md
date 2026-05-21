# EspUsbHostGamepad

> 日本語版: [README.ja.md](README.ja.md)

Reads USB HID gamepad reports and prints raw report bytes plus simple hat/button candidates to the Serial monitor.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB HID gamepad or joystick

## What it does

- Prints the parsed report bytes without interpreting axes
- Leaves scaling, deadzones, and multi-byte sensor/axis interpretation to application code
- Prints the hat candidate value when one is present
- Shows button press and release deltas alongside the current bitmask

## Key APIs

- `usb.onGamepad(callback)` — fired on each gamepad report with `EspUsbHostGamepadEvent`
  - `event.hat` — raw hat candidate value
  - `event.hasHat` — true when a hat candidate was present in the parsed report
  - `event.buttons` — 32-bit button bitmask (current state)
  - `event.previousButtons` — previous button bitmask
  - `event.rawData`, `event.rawLength` — raw HID input report bytes
  - `event.reportData`, `event.reportLength` — gamepad report bytes after removing the Report ID when one is present

## Expected Serial output

```
connected: device: address=1 portId=0x01 vid=054c pid=09cc class=0x00(Device) speed=full product="Wireless Controller"
report[11]=00 00 00 00 00 00 00 00 00 00 00 hat=0 buttons=0x00000000
report[11]=0A F6 14 EC 1E E2 03 05 00 00 00 hat=3 buttons=0x00000005 +0x5
```

> **Note:** Axis order, axis meaning, and button layout vary by device and HID report descriptor.
> `onGamepad()` intentionally does not name axes as left/right stick fields or normalize values. Use `event.rawData` / `event.rawLength` or `event.reportData` / `event.reportLength` to inspect the actual report and add device-specific mapping, including 16-bit axes or 12-bit sensor fields.
