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
| `ESP_USB_HOST_SYSTEM_CONTROL_POWER_OFF` | `0x81` | Power Off |
| `ESP_USB_HOST_SYSTEM_CONTROL_STANDBY` | `0x82` | Standby |
| `ESP_USB_HOST_SYSTEM_CONTROL_WAKE_HOST` | `0x83` | Wake Host |

## Key APIs

- `usb.onSystemControl(callback)` — fired on press/release with `EspUsbHostSystemControlEvent`
  - `event.pressed` — true on press, false on release
  - `event.usage` — HID usage code (8-bit)

## Expected Serial output

```
system press usage=0x81 Power Off
system release usage=0x81 Power Off
```
