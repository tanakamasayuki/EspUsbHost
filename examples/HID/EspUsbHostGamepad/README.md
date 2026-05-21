# EspUsbHostGamepad

> 日本語版: [README.ja.md](README.ja.md)

Reads USB HID gamepad reports and prints raw report bytes plus descriptor-decoded fields to the Serial monitor.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB HID gamepad or joystick

## What it does

- Prints the parsed report bytes and HID fields decoded from the report descriptor
- Leaves axis assignment, scaling, deadzones, and device-specific mapping to application code
- Includes an example mapper that assigns Generic Desktop X/Y and Button usages to an application struct

## Key APIs

- `usb.onGamepad(callback)` — fired on each gamepad report with `EspUsbHostGamepadEvent`
  - `event.fields`, `event.fieldCount` — decoded HID input fields from the report descriptor
  - `field.usagePage`, `field.usage` — HID usage identity, such as Generic Desktop `X` (`0x01:0x30`) or Button 1 (`0x09:0x01`)
  - `field.value`, `field.logicalMin`, `field.logicalMax` — decoded value and logical range
  - `field.bitOffset`, `field.bitSize`, `field.flags` — source location and Input item flags
  - `event.rawData`, `event.rawLength` — raw HID input report bytes
  - `event.reportData`, `event.reportLength` — gamepad report bytes after removing the Report ID when one is present

## Expected Serial output

```
connected: device: address=1 portId=0x01 vid=054c pid=09cc class=0x00(Device) speed=full product="Wireless Controller"
report[11]=0A F6 14 EC 1E E2 03 05 00 00 00
  field[0] page=0x0001 usage=0x0030 value=10 logical=-127..127 bits=8@0 flags=0x02
  field[1] page=0x0001 usage=0x0031 value=-10 logical=-127..127 bits=8@8 flags=0x02
  field[7] page=0x0009 usage=0x0001 value=1 logical=0..1 bits=1@56 flags=0x02
```

> **Note:** Axis order, axis meaning, and button layout vary by device and HID report descriptor.
> `onGamepad()` intentionally does not name axes as left/right stick fields or normalize values. Use `event.fields`, `event.rawData` / `event.rawLength`, or `event.reportData` / `event.reportLength` to add VID/PID-specific mapping, including 16-bit axes or 12-bit sensor fields.
