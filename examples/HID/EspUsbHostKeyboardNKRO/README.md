# EspUsbHost KeyboardNKRO

> 日本語版: [README.ja.md](README.ja.md)

Hosts an N-key rollover (NKRO) USB keyboard and reports how many keys are held at
the same time. A boot keyboard is limited to 6 simultaneous keys; an NKRO keyboard
sends a key **bitmap** instead, lifting that limit.

EspUsbHost decodes both formats automatically — it learns the report layout from
the HID report descriptor, so `onKeyboardState()` delivers the same normalized
key state whether the keyboard is boot (6KRO) or NKRO. No configuration is required.

## Hardware

- ESP32-S3 (or another Arduino-ESP32 board with USB host support) as the host
- An NKRO USB keyboard, or a second ESP32-S3 running the sibling
  [EspUsbDevice `KeyboardNKRO`](https://github.com/tanakamasayuki/EspUsbDevice/tree/main/examples/KeyboardNKRO) sketch
- A separate Serial monitor connection for logs

## What It Does

- Prints each key press/release and the running count of keys held together
- Tracks the maximum number of simultaneously-held keys (a value above 6 proves
  NKRO is in effect)
- On connect, prints whether an NKRO bitmap report was detected

## Key APIs

- `usb.onKeyboardState(cb)` delivers one `EspUsbHostKeyboardState` for each changed
  report. `isDown()`, `wasPressed()`, and `wasReleased()` cover every Keyboard/Keypad
  usage, including modifiers `0xE0-0xE7`, identically for boot and NKRO keyboards.
- `usb.keyboardUsesBitmapReport(address)` reports whether the keyboard sends an
  NKRO bitmap (report protocol) rather than the 6-key boot report. Diagnostic
  only; decoding is automatic.

## Expected Serial Output

Holding seven keys (`a`–`g`) together, then releasing one:

```
Keyboard connected: 303a:4033 nkro-bitmap=1
press   keycode=0x04
held=1 (max=1) modifiers=0x00
press   keycode=0x05
held=2 (max=2) modifiers=0x00
press   keycode=0x06
held=3 (max=3) modifiers=0x00
press   keycode=0x07
held=4 (max=4) modifiers=0x00
press   keycode=0x08
held=5 (max=5) modifiers=0x00
press   keycode=0x09
held=6 (max=6) modifiers=0x00
press   keycode=0x0a
held=7 (max=7) modifiers=0x00
release keycode=0x04
held=6 (max=7) modifiers=0x00
```

A `held` count above 6 shows NKRO is in effect.

## Notes

- NKRO keyboards default to report protocol, where they send the bitmap. The host
  never forces boot protocol, so NKRO stays active.
- International1-9 and LANG1-9 usages are inside the bitmap range, so JIS and other
  non-US keys are reported too.

## See Also

- [EspUsbHostKeyboard](../EspUsbHostKeyboard/) - standard boot keyboard example
- [EspUsbDevice KeyboardNKRO](https://github.com/tanakamasayuki/EspUsbDevice/tree/main/examples/KeyboardNKRO) - the
  matching NKRO device
