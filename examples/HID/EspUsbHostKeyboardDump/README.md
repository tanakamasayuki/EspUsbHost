# EspUsbHostKeyboardDump

> 日本語版: [README.ja.md](README.ja.md)

Dumps the parsed fields of every keyboard event to the Serial monitor: press/release state, HID keycode, ASCII value, and active modifier keys.

Useful for debugging custom keyboards, verifying keycode mapping, or understanding what data the library produces before it is interpreted as characters.

## Hardware

- ESP32-S3 (or another board supported by Arduino-ESP32 USB Host)
- USB HID keyboard

## What it does

- Prints all fields of `EspUsbHostKeyboardEvent` for every key press and release
- Modifier bits are decoded into named flags (LSHIFT, RCTRL, etc.)
- No layout is set; keycode and ascii reflect the raw HID report

## Key APIs

- `usb.onKeyboard(callback)` — fired on each key press/release with `EspUsbHostKeyboardEvent`
- `event.keycode` / `event.ascii` / `event.modifiers` — parsed keyboard fields used by the custom callback in this example
- `event.rawData` / `event.rawLength` and `event.reportData` / `event.reportLength` — access to the original HID report bytes alongside the parsed keyboard fields

## Expected Serial output

```
connected: device: address=1 portId=0x01 vid=045e pid=07a5 class=0x00(Device) speed=full product="USB Keyboard"
[press  ] keycode=0x04 ascii=0x61(a) modifiers=none
[release] keycode=0x04 ascii=0x61(a) modifiers=none
[press  ] keycode=0x04 ascii=0x41(A) modifiers=LSHIFT
[release] keycode=0x04 ascii=0x41(A) modifiers=LSHIFT
[press  ] keycode=0x39 ascii=0x00(.) modifiers=none
[release] keycode=0x39 ascii=0x00(.) modifiers=none
```

## Difference from EspUsbHostHIDRawDump

`EspUsbHostHIDRawDump` shows the raw bytes from the USB transfer before any parsing.
This example shows the library's parsed view: the same event after keycode lookup and ASCII conversion.

## See also

- [EspUsbHostKeyboard](../EspUsbHostKeyboard/) — character output with LED sync
- [EspUsbHostHIDRawDump](../EspUsbHostHIDRawDump/) — raw HID report bytes

## Walkthrough and checks

### Before running

Inspect keyboard usages, modifiers and translated characters. Follow the hardware and dependency requirements above, select the board and the example's profile, and locate the diagnostic Serial connection. [Example workflow](../../../docs/example-workflow.md) explains installation, connectors, USB roles and the difference between startup and a usable peer.

### Follow the code

`onKeyboard()` prints both press and release events. Compare a letter alone, Shift plus the letter, and a nonprinting key: usage identifies the key while modifiers and layout determine its interpretation. Use this before changing a keymap.

### API use and adaptation

A HID report is a structured state or change, not an arbitrary text line. Decode it with its report descriptor: distinguish report ID, usage page/usage, signed value and logical range. Keyboard usages identify key positions, mouse motion is relative, and button values are bitmaps. Match press with release, or compare complete states, before adding an application action.

Callback report data is borrowed. Copy the bytes or decoded values into owned storage before handing work to another task. Log only short diagnostics in the callback; for repeated raw dumps, terminal speed can become the bottleneck. If an input is missing, first confirm enumeration, then descriptor/report length, then application decoding. Do not change all three at once.

### Verify one change at a time

Run the unchanged example first and compare the operation above with its logs or host application. Test the peer absent and then connected; note which stage succeeds instead of expecting the final result immediately. Reset to the original configuration before changing one pin, format or command. The demonstration's error logging is not a general retry or recovery policy.

- [Sketch](EspUsbHostKeyboardDump.ino)
- [All examples](../../README.md)
- [Example workflow](../../../docs/example-workflow.md)
