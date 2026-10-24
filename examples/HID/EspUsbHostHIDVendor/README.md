# EspUsbHostHIDVendor

> 日本語版: [README.ja.md](README.ja.md)

Demonstrates vendor-class HID communication: receiving input reports and sending output/feature reports to a USB HID vendor device.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB HID vendor device (typically an ESP32-S3 running a vendor HID peripheral sketch)

## What it does

- Receives vendor HID input reports and prints their contents to Serial
- Sends a fixed 63-byte output report or feature report on Serial command

## Serial commands

| Command | Action |
|---------|--------|
| `o` | Send output report (`"host output"`) |
| `f` | Send feature report (`"host feature"`) |

## Key APIs

- `usb.onHIDVendorInput(callback)` — fired on each vendor HID input report with `EspUsbHostHIDVendorInput`
  - `input.interfaceNumber` — HID interface number
  - `input.rawData`, `input.rawLength` — raw HID input report bytes
  - `input.reportData`, `input.reportLength` — report bytes after removing the Report ID when one is present
- `usb.sendHIDVendorOutput(data, length)` — sends the supplied bytes unchanged through interrupt OUT. Pass `[Report ID, payload…]` for a numbered report, or only the payload for an unnumbered report. No ID is inserted or removed. Account for this when porting from an API that takes the ID as a separate argument.
- `usb.sendHIDVendorFeature(data, length)` — sends an HID feature report through EP0 `SET_REPORT`

## Expected Serial output

```
connected: device: address=1 portId=0x01 vid=303a pid=4004 class=0x00(Device) speed=full product="ESP32S3 HID Vendor"
vendor iface=0 len=63 data=device input
send output: ok
send feature: ok
```

## Walkthrough and checks

### Before running

Receive vendor HID input and send matching output or feature reports. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

The UART commands o and f send the predefined output and feature data. `onHIDVendorInput()` shows the reply. Use a peer implementing that descriptor and protocol; report IDs and payload length must agree on both ends, and Vendor HID is different from vendor bulk.

### API use and adaptation

A HID report is a structured state or change, not an arbitrary text line. Decode it with its report descriptor: distinguish report ID, usage page/usage, signed value and logical range. Keyboard usages identify key positions, mouse motion is relative, and button values are bitmaps. Match press with release, or compare complete states, before adding an application action.

Callback report data is borrowed. Copy the bytes or decoded values into owned storage before handing work to another task. Log only short diagnostics in the callback; for repeated raw dumps, terminal speed can become the bottleneck. If an input is missing, first confirm enumeration, then descriptor/report length, then application decoding. Do not change all three at once.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostHIDVendor.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
