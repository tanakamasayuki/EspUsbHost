# EspUsbHostHIDVendor

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

- `usb.onVendorInput(callback)` — fired on each vendor HID input report with `EspUsbHostVendorInput`
  - `input.interfaceNumber` — HID interface number
  - `input.length` — report byte length
  - `input.data` — report payload
- `usb.sendVendorOutput(data, length)` — sends an HID output report
- `usb.sendVendorFeature(data, length)` — sends an HID feature report

## Expected Serial output

```
connected: vid=303a pid=4004 product=ESP32S3 HID Vendor
vendor iface=0 len=63 data=device input
send output: ok
send feature: ok
```
