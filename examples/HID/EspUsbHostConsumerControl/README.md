# EspUsbHostConsumerControl

Receives HID consumer control events (media keys) from a USB keyboard or remote and prints them to the Serial monitor.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB keyboard or remote with consumer control keys (volume, play/pause, etc.)

## What it does

- Listens for HID consumer control reports
- Prints each key press and release with its HID usage code and a human-readable name for common codes

Common consumer control usage codes handled:

| Usage | Name |
|-------|------|
| `0x00e2` | Mute |
| `0x00e9` | Volume Up |
| `0x00ea` | Volume Down |
| `0x00cd` | Play/Pause |
| `0x00b5` | Next Track |
| `0x00b6` | Previous Track |

## Key APIs

- `usb.onConsumerControl(callback)` — fired on press/release with `EspUsbHostConsumerControlEvent`
  - `event.pressed` — true on press, false on release
  - `event.usage` — HID usage code (16-bit)

## Expected Serial output

```
connected: vid=045e pid=07a5 product=USB Keyboard
consumer press usage=0x00e9 Volume Up
consumer release usage=0x00e9 Volume Up
consumer press usage=0x00cd Play/Pause
consumer release usage=0x00cd Play/Pause
```
