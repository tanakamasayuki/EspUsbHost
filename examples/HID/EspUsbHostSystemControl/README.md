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

## Walkthrough and checks

### Before running

Receive system-control HID inputs. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

`onSystemControl()` distinguishes usages and press/release. Test only a control your attached device actually exposes; ordinary letter keys will not enter this callback. The example observes reports and does not itself shut down another computer.

### API use and adaptation

A HID report is a structured state or change, not an arbitrary text line. Decode it with its report descriptor: distinguish report ID, usage page/usage, signed value and logical range. Keyboard usages identify key positions, mouse motion is relative, and button values are bitmaps. Match press with release, or compare complete states, before adding an application action.

Callback report data is borrowed. Copy the bytes or decoded values into owned storage before handing work to another task. Log only short diagnostics in the callback; for repeated raw dumps, terminal speed can become the bottleneck. If an input is missing, first confirm enumeration, then descriptor/report length, then application decoding. Do not change all three at once.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostSystemControl.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
