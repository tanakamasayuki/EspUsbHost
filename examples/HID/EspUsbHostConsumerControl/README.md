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

## Walkthrough and checks

### Before running

Receive media-key press and release events. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

Use `onConsumerControl()` for volume, playback and similar usages. Press and release a media key and compare event.pressed; these usages belong to a different HID page from normal keyboard keys. The ESP32 logs input rather than controlling the PC's volume.

### API use and adaptation

A HID report is a structured state or change, not an arbitrary text line. Decode it with its report descriptor: distinguish report ID, usage page/usage, signed value and logical range. Keyboard usages identify key positions, mouse motion is relative, and button values are bitmaps. Match press with release, or compare complete states, before adding an application action.

Callback report data is borrowed. Copy the bytes or decoded values into owned storage before handing work to another task. Log only short diagnostics in the callback; for repeated raw dumps, terminal speed can become the bottleneck. If an input is missing, first confirm enumeration, then descriptor/report length, then application decoding. Do not change all three at once.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostConsumerControl.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
