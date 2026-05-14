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

## Expected Serial output

```
connected: vid=045e pid=07a5 product=USB Keyboard
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
