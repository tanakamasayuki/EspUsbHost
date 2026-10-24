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

## Walkthrough and checks

### Before running

Inspect gamepad fields using their HID usages. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

`onGamepad()` exposes decoded fields as well as raw bytes. Move one axis or press one button at a time and match usagePage/usage to field.value. The device-specific interpretation in the sketch is an example, not a universal report layout.

### API use and adaptation

A HID report is a structured state or change, not an arbitrary text line. Decode it with its report descriptor: distinguish report ID, usage page/usage, signed value and logical range. Keyboard usages identify key positions, mouse motion is relative, and button values are bitmaps. Match press with release, or compare complete states, before adding an application action.

Callback report data is borrowed. Copy the bytes or decoded values into owned storage before handing work to another task. Log only short diagnostics in the callback; for repeated raw dumps, terminal speed can become the bottleneck. If an input is missing, first confirm enumeration, then descriptor/report length, then application decoding. Do not change all three at once.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostGamepad.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
