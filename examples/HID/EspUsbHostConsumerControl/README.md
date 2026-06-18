# EspUsbHostConsumerControl

> 日本語版: [README.ja.md](README.ja.md)

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
| `ESP_USB_HOST_CONSUMER_CONTROL_MUTE` | Mute |
| `ESP_USB_HOST_CONSUMER_CONTROL_VOLUME_UP` | Volume Up |
| `ESP_USB_HOST_CONSUMER_CONTROL_VOLUME_DOWN` | Volume Down |
| `ESP_USB_HOST_CONSUMER_CONTROL_PLAY_PAUSE` | Play/Pause |
| `ESP_USB_HOST_CONSUMER_CONTROL_NEXT_TRACK` | Next Track |
| `ESP_USB_HOST_CONSUMER_CONTROL_PREVIOUS_TRACK` | Previous Track |

## Key APIs

- `usb.onConsumerControl(callback)` — fired on press/release with `EspUsbHostConsumerControlEvent`
  - `event.pressed` — true on press, false on release
  - `event.usage` — HID usage code (16-bit)
  - `event.rawData`, `event.rawLength` — raw HID input report bytes
  - `event.reportData`, `event.reportLength` — consumer control report bytes after removing the Report ID when one is present
- `espUsbHostConsumerControlUsageName(event.usage)` — returns a readable name for common consumer usages

## Expected Serial output

```
connected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
consumer press usage=0x00e9 Volume Up
consumer release usage=0x00e9 Volume Up
consumer press usage=0x00cd Play/Pause
consumer release usage=0x00cd Play/Pause
```
