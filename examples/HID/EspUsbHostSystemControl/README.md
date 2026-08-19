# EspUsbHostSystemControl

> 日本語版: [README.ja.md](README.ja.md)

Receives HID system control events (power, standby, wake) from a USB keyboard and prints them to the Serial monitor.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB keyboard with system control keys (power button, etc.)

## What it does

- Listens for HID system control reports
- Prints each press and release with its HID usage code and name

Supported usage codes (defined in `EspUsbHost.h`):

| Constant | Value | Name |
|----------|-------|------|
| `ESP_USB_HOST_SYSTEM_CONTROL_POWER_OFF` | `0x01` | Power Off |
| `ESP_USB_HOST_SYSTEM_CONTROL_STANDBY` | `0x02` | Standby |
| `ESP_USB_HOST_SYSTEM_CONTROL_WAKE_HOST` | `0x03` | Wake Host |

The value in `event.usage` is the first byte of the system control report, passed through without translation, so what a given key produces depends on the keyboard's report descriptor. A common encoding is an array item whose logical values start at 1, which yields the `0x01`–`0x03` values above. Keyboards that report the Generic Desktop usage ID itself send `0x81` (System Power Down), `0x82` (System Sleep) or `0x83` (System Wake Up) instead; those arrive unchanged, and `espUsbHostSystemControlUsageName()` returns an empty name for them. Check the `usage=0x..` value printed for your device to see which encoding it uses.

## Key APIs

- `usb.onSystemControl(callback)` — fired on press/release with `EspUsbHostSystemControlEvent`
  - `event.pressed` — true on press, false on release
  - `event.usage` — HID usage code (8-bit)
  - `event.rawData`, `event.rawLength` — raw HID input report bytes
  - `event.reportData`, `event.reportLength` — system control report bytes after removing the Report ID when one is present
- `espUsbHostSystemControlUsageName(event.usage)` — returns a readable name for common system usages

## Expected Serial output

```
system press usage=0x01 Power Off
system release usage=0x01 Power Off
```
